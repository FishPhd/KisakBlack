"""Whole-EXE structural comparison. This never establishes behavioral equivalence."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import uuid
from contextlib import closing

from .core import ROOT, WORK, Blocked, digest, reference, read_json, write_json, canonical_hash
from .indexer import ghidra_home, GHIDRA_VERSION

SCRIPT = ROOT / "tools/re/ghidra/ExportBinaryDiff.java"
DEFAULT_BINDIFF = WORK / "vendor/bindiff/ProgramFiles/BinDiff/bin/bindiff.exe"


def logged(command, log, env=None, timeout=3600):
    """Keep diagnostics even when external tools fail or time out."""
    with Path(log).open("wb") as stream:
        try:
            result = subprocess.run([str(x) for x in command], stdout=stream,
                                    stderr=subprocess.STDOUT, env=env, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise Blocked(f"Timed out; diagnostics: {log}") from error
    if result.returncode:
        raise Blocked(f"Tool exited {result.returncode}; diagnostics: {log}")


def load_inventory(path):
    rows = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines()]
    if not rows or rows[0].get("record") != "metadata" or rows[-1].get("record") != "complete":
        raise Blocked("Incomplete binary-diff inventory")
    functions = [r for r in rows if r.get("record") == "function"]
    if not functions or rows[-1]["functions"] != len(functions):
        raise Blocked("Empty or inconsistent binary-diff inventory")
    addresses = [r["address"] for r in functions]
    if len(set(addresses)) != len(addresses) or any(r["bytes"] < 0 or r["instructions"] < 0 for r in functions):
        raise Blocked("Invalid function inventory")
    return {"metadata": rows[0], "functions": functions, "completion": rows[-1]}


def export_binary(path, home, refresh=False):
    extension = home / "Ghidra/Extensions/BinExport"
    jars = sorted(extension.glob("lib/*.jar"))
    if not jars:
        raise Blocked("BinExport extension missing; run tools/re/setup-binary-diff.ps1")
    identity = {"binary_sha256": digest(path), "script_sha256": digest(SCRIPT),
                "ghidra": GHIDRA_VERSION, "extension": {p.name: digest(p) for p in jars}}
    # PDB changes affect names/matches even when the executable is unchanged.
    pdb = path.with_suffix(".pdb")
    identity["adjacent_pdb_sha256"] = digest(pdb) if pdb.is_file() else None
    key = canonical_hash(identity)
    cache = WORK / "binary-diff/exports" / key
    manifest_path = cache / "manifest.json"
    if manifest_path.exists() and not refresh:
        manifest = read_json(manifest_path)
        if manifest["identity"] != identity:
            raise Blocked("Export identity mismatch")
        for name, value in manifest["files"].items():
            if digest(cache / name) != value:
                raise Blocked("Export cache modified; rerun with --refresh")
        print(f"Using checked export: {path.name}", file=sys.stderr, flush=True)
        return cache
    # Unique staging directories prevent incomplete runs or concurrent exports
    # from being mistaken for complete caches. Retain staging on failures.
    stage = WORK / "binary-diff/staging" / uuid.uuid4().hex
    stage.mkdir(parents=True)
    env = dict(os.environ)
    env.update(XDG_CONFIG_HOME=str(WORK / "settings"), XDG_CACHE_HOME=str(WORK / "cache"))
    java = WORK / "vendor/jdk-21.0.10+7"
    if java.exists():
        env["JAVA_HOME"] = str(java)
    reused = None
    if not refresh:
        # Reporter/export-script edits do not require reanalyzing the executable.
        # Reuse only a completed, hash-checked export's saved analysis, read-only.
        for prior_path in sorted((WORK / "binary-diff/exports").glob("*/manifest.json")):
            prior = read_json(prior_path)
            if any(prior["identity"].get(k) != identity[k] for k in (
                    "binary_sha256", "ghidra", "extension", "adjacent_pdb_sha256")):
                continue
            project = Path(prior.get("analysis_project", Path(prior["analysis_log"]).parent))
            if (project / "Analysis.gpr").exists() and all(
                    digest(prior_path.parent / n) == h for n, h in prior["files"].items()):
                reused = {"project": str(project), "program": Path(prior["input_path"]).name,
                          "manifest": str(prior_path), "manifest_sha256": digest(prior_path)}
                break
    command = [home / "support/launch.bat", "fg", "jdk", "Ghidra-Headless", "8G",
               "-XX:ParallelGCThreads=2 -XX:CICompilerCount=2", "ghidra.app.util.headless.AnalyzeHeadless"]
    command += ([reused["project"], "Analysis", "-process", reused["program"], "-readOnly", "-noanalysis"]
                if reused else [stage, "Analysis", "-import", path])
    command += ["-max-cpu", "4", "-scriptPath", SCRIPT.parent,
                "-postScript", SCRIPT.name, stage / "program.BinExport", stage / "inventory.jsonl"]
    log = stage / "analysis.log"
    print(f"{'Exporting saved analysis' if reused else 'Analyzing and exporting'}: {path}\nLog: {log}", file=sys.stderr, flush=True)
    logged(command, log, env)
    output = log.read_text(errors="replace")
    if "KISAK_BINARY_DIFF_EXPORT_COMPLETE" not in output or any(s in output for s in (
            "OutOfMemoryError", "REPORT SCRIPT ERROR", "Analysis timed out")):
        raise Blocked(f"Incomplete Ghidra analysis/export; diagnostics: {log}")
    inventory = load_inventory(stage / "inventory.jsonl")
    if inventory["metadata"]["sha256"].lower() != identity["binary_sha256"] or digest(path) != identity["binary_sha256"]:
        raise Blocked("Executable changed during export")
    if (digest(pdb) if pdb.is_file() else None) != identity["adjacent_pdb_sha256"]:
        raise Blocked("Adjacent PDB changed during export")
    if digest(SCRIPT) != identity["script_sha256"] or {p.name: digest(p) for p in jars} != identity["extension"]:
        raise Blocked("Export script or extension changed during analysis; rerun")
    manifest = {"identity": identity, "files": {name: digest(stage / name) for name in ("program.BinExport", "inventory.jsonl")},
                "input_path": str(path), "analysis_log": str(log), "created_unix": time.time(),
                "analysis_project": reused["project"] if reused else str(stage), "reused_analysis": reused}
    cache.mkdir(parents=True, exist_ok=True)
    # Publish the manifest last; consumers validate the content hashes.
    from .core import atomic_bytes
    for name in manifest["files"]:
        atomic_bytes(cache / name, (stage / name).read_bytes())
    write_json(manifest_path, manifest)
    return cache


def read_matches(database, original, candidate):
    with closing(sqlite3.connect(Path(database).resolve().as_uri() + "?mode=ro", uri=True)) as db:
        db.row_factory = sqlite3.Row
        metadata = dict(db.execute("SELECT * FROM metadata").fetchone())
        files = {r["id"]: dict(r) for r in db.execute("SELECT * FROM file")}
        for key, inventory in (("file1", original), ("file2", candidate)):
            if files[metadata[key]]["hash"].lower() != inventory["metadata"]["sha256"].lower():
                raise Blocked("BinDiff database input identity/order mismatch")
        matches = [dict(r) for r in db.execute(
            "SELECT f.*, a.name AS match_algorithm FROM function f LEFT JOIN functionalgorithm a ON a.id=f.algorithm")]
    seen1, seen2 = set(), set()
    for r in matches:
        if r["address1"] in seen1 or r["address2"] in seen2:
            raise Blocked("Ambiguous many-to-one BinDiff matches")
        seen1.add(r["address1"]); seen2.add(r["address2"])
        if any(not isinstance(r[k], (int, float)) or not math.isfinite(r[k]) or not 0 <= r[k] <= 1 for k in ("similarity", "confidence")):
            raise Blocked("Invalid BinDiff similarity/confidence")
    return matches, metadata


def summarize(original, candidate, matches, min_confidence):
    if not math.isfinite(min_confidence) or not 0 <= min_confidence <= 1:
        raise Blocked("Minimum confidence must be in [0, 1]")
    left = {f["address"]: f for f in original["functions"]}
    right = {f["address"]: f for f in candidate["functions"]}
    by_address = {m["address1"]: m for m in matches}
    rows, used, numerator, covered = [], set(), 0.0, 0
    weight = sum(f["bytes"] for f in left.values())
    if not weight:
        raise Blocked("No original decoded function bytes to compare")
    for address, f in left.items():
        m = by_address.get(address)
        other = right.get(m["address2"]) if m else None
        # Imported/external-only matches are outside the decoded-body score.
        accepted = bool(other and m["confidence"] >= min_confidence)
        if other:
            used.add(other["address"])
        score = m["similarity"] * 100 if other else None
        status = "matched" if accepted else "low_confidence" if other else "unmatched"
        if accepted:
            numerator += f["bytes"] * m["similarity"]
            covered += f["bytes"]
        rows.append({"original": f, "candidate": other, "status": status,
                     "similarity_percent": score, "confidence": m["confidence"] if other else None,
                     "match_algorithm": m["match_algorithm"] if other else None,
                     "matched_instructions": m["instructions"] if other else 0,
                     "matched_basic_blocks": m["basicblocks"] if other else 0,
                     "matched_edges": m["edges"] if other else 0,
                     "weighted_contribution_percent": 100 * f["bytes"] * m["similarity"] / weight if accepted else 0})
    extra = [f for a, f in right.items() if a not in used]
    return {"overall_similarity_percent": 100 * numerator / weight,
            "matched_only_similarity_percent": 100 * numerator / covered if covered else None,
            "matched_original_byte_coverage_percent": 100 * covered / weight,
            "original_functions": len(left), "candidate_functions": len(right),
            "accepted_matches": sum(r["status"] == "matched" for r in rows),
            "low_confidence_matches": sum(r["status"] == "low_confidence" for r in rows),
            "unmatched_original_functions": sum(r["status"] == "unmatched" for r in rows),
            "unmatched_candidate_functions": len(extra), "original_function_byte_weight": weight,
            "minimum_confidence": min_confidence, "rows": rows, "candidate_only": extra}


def render_report(value, target):
    template = (ROOT / "tools/re/binary-diff-report.html").read_text(encoding="utf-8")
    payload = json.dumps(value, ensure_ascii=True, allow_nan=False).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    target.write_text(template.replace("__REPORT_DATA__", payload), encoding="utf-8")


def compare(candidate, binary=None, ghidra=None, bindiff=None, min_confidence=.7, refresh=False):
    import pefile
    original_path, original_pe = reference(binary)
    candidate_path = Path(candidate).resolve()
    if not candidate_path.is_file():
        raise Blocked(f"Candidate executable not found: {candidate_path}")
    with pefile.PE(str(candidate_path)) as pe:
        if pe.FILE_HEADER.Machine != original_pe.FILE_HEADER.Machine:
            raise Blocked("Candidate must match original x86 architecture")
    engine = Path(bindiff or os.environ.get("BINDIFF_EXE", DEFAULT_BINDIFF)).resolve()
    if not engine.is_file():
        raise Blocked("BinDiff missing; run tools/re/setup-binary-diff.ps1 or pass --bindiff")
    if not math.isfinite(min_confidence) or not 0 <= min_confidence <= 1:
        raise Blocked("Minimum confidence must be in [0, 1]")
    home = ghidra_home(ghidra)
    primary = export_binary(original_path, home, refresh)
    secondary = export_binary(candidate_path, home, refresh)
    original = load_inventory(primary / "inventory.jsonl")
    candidate_inventory = load_inventory(secondary / "inventory.jsonl")
    output = WORK / "binary-diff/reports" / f"run-{time.time_ns()}"
    output.mkdir(parents=True)
    # Keep exports alongside the database so it can be opened in BinDiff UI.
    import shutil
    from .binexport_repair import repair_binexport
    for cache, name in ((primary, "original"), (secondary, "candidate")):
        repaired, changes = repair_binexport((cache / "program.BinExport").read_bytes())
        (output / f"{name}.BinExport").write_bytes(repaired)
        print(f"Prepared {name} BinExport for BinDiff: {changes}", file=sys.stderr, flush=True)
    print(f"Matching whole executables with BinDiff; output: {output}", file=sys.stderr, flush=True)
    logged([engine, "--print_config"], output / "engine-config.json", timeout=60)
    logged([engine, f"--primary={output / 'original.BinExport'}", f"--secondary={output / 'candidate.BinExport'}",
            f"--output_dir={output}", "--output_format=bin"], output / "bindiff.log")
    databases = list(output.glob("*.BinDiff"))
    if len(databases) != 1:
        raise Blocked(f"Expected one BinDiff database; inspect {output / 'bindiff.log'}")
    matches, metadata = read_matches(databases[0], original, candidate_inventory)
    summary = summarize(original, candidate_inventory, matches, min_confidence)
    value = {"schema": 1, "metric": "compiled-code structural similarity", "created_unix": time.time(),
             "summary": summary, "original": original["metadata"], "candidate": candidate_inventory["metadata"],
             "discovery": {"original": original["completion"], "candidate": candidate_inventory["completion"]},
             "bindiff_metadata": metadata, "database": databases[0].name,
             "provenance": {"original_path": str(original_path), "candidate_path": str(candidate_path),
                 "original_export": read_json(primary / "manifest.json"), "candidate_export": read_json(secondary / "manifest.json"),
                 "bindiff_sha256": digest(engine), "database_sha256": digest(databases[0]),
                 "engine_config_sha256": digest(output / "engine-config.json"),
                 "reporter_sha256": digest(Path(__file__)), "template_sha256": digest(ROOT / "tools/re/binary-diff-report.html")},
             "limits": "Static heuristic similarity, not behavioral accuracy. All discovered internal functions, including library code and thunks, are in scope. Undiscovered code, data, resources and external DLL implementations are not scored. Shared instructions count once per owning function. Unmatched and below-threshold functions receive zero demonstrated similarity. Compiler/optimization differences and inlining affect matching. Names/source locations depend on available symbols. Reports describe the supplied EXEs, not unbuilt source changes."}
    write_json(output / "report.json", value)
    render_report(value, output / "report.html")
    write_json(WORK / "binary-diff/latest.json", {"report": str(output / "report.html"), "json": str(output / "report.json")})
    return {"status": "complete", "metric": value["metric"],
            "summary": {k: v for k, v in summary.items() if k not in ("rows", "candidate_only")},
            "report": str(output / "report.html"), "json": str(output / "report.json"), "database": str(databases[0])}

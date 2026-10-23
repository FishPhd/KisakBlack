"""Isolated BinDiff iteration driver. Does not add re.ps1 commands.

A run without --quick calls retool.binary_diff.compare. Repaired exports are
cached under build-re/isolated/mp-match-iteration so a repeat of the same EXEs skips the BinExport parse.
--quick is an iteration score: when the candidate has no completed full export,
Ghidra skips decompiler-switch analysis. The recorded score remains a full
analysis from tools/re/re.ps1 binary-diff.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "tools" / "re"))

from retool.binary_diff import (  # noqa: E402
    SCRIPT, export_binary, compare, load_inventory, logged,
)
from retool.binexport_repair import repair_binexport  # noqa: E402
from retool.core import WORK, Blocked, digest, canonical_hash  # noqa: E402
from retool.indexer import GHIDRA_VERSION, ghidra_home  # noqa: E402
import retool.binary_diff as binary_diff  # noqa: E402
import retool.binexport_repair as repair_mod  # noqa: E402


LOCAL_WORK = WORK / "isolated" / "mp-match-iteration"
REPAIR_CACHE = LOCAL_WORK / "repair-cache"
QUICK_EXPORTS = LOCAL_WORK / "quick-exports"
PRE_SCRIPT = HERE / "DisableDecompilerSwitch.java"


def watch_names():
    names = []
    for line in (HERE / "watchlist.txt").read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            names.append(line)
    return names


def leaf(name):
    name = (name or "").split("(")[0]
    if "::" in name:
        name = name.split("::")[-1]
    return name


def install_repair_cache():
    code = hashlib.sha256(Path(repair_mod.__file__).read_bytes()).hexdigest()
    original = repair_binexport

    def cached(data):
        if isinstance(data, memoryview):
            data = bytes(data)
        key = hashlib.sha256(code.encode() + data).hexdigest()
        folder = REPAIR_CACHE / key
        blob = folder / "program.BinExport"
        meta = folder / "changes.json"
        if blob.is_file() and meta.is_file():
            print(f"Using repaired export cache: {folder}", file=sys.stderr, flush=True)
            return blob.read_bytes(), json.loads(meta.read_text(encoding="utf-8"))
        repaired, changes = original(data)
        folder.mkdir(parents=True, exist_ok=True)
        blob.write_bytes(repaired)
        meta.write_text(json.dumps(changes), encoding="utf-8")
        return repaired, changes

    repair_mod.repair_binexport = cached


def export_identity(path, home):
    extension = home / "Ghidra/Extensions/BinExport"
    jars = sorted(extension.glob("lib/*.jar"))
    if not jars:
        raise Blocked("BinExport extension missing; run tools/re/setup-binary-diff.ps1")
    identity = {"binary_sha256": digest(path), "script_sha256": digest(SCRIPT),
                "ghidra": GHIDRA_VERSION, "extension": {p.name: digest(p) for p in jars}}
    pdb = Path(path).with_suffix(".pdb")
    identity["adjacent_pdb_sha256"] = digest(pdb) if pdb.is_file() else None
    return identity


def full_export_ready(path, home):
    key = canonical_hash(export_identity(path, home))
    manifest = WORK / "binary-diff" / "exports" / key / "manifest.json"
    return manifest.is_file()


def quick_export(path, home):
    """Headless export that skips decompiler-switch analysis. Not a recorded score."""
    path = Path(path).resolve()
    identity = export_identity(path, home)
    identity["analysis"] = "quick-no-decompiler-switch"
    identity["prescript_sha256"] = digest(PRE_SCRIPT)
    folder = QUICK_EXPORTS / canonical_hash(identity)
    manifest_path = folder / "manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("identity") == identity and (folder / "program.BinExport").is_file():
            print(f"Using quick export cache: {folder}", file=sys.stderr, flush=True)
            return folder
    stage = LOCAL_WORK / "quick-staging" / uuid.uuid4().hex
    stage.mkdir(parents=True)
    env = dict(os.environ)
    env.update(XDG_CONFIG_HOME=str(WORK / "settings"), XDG_CACHE_HOME=str(WORK / "cache"))
    java = WORK / "vendor/jdk-21.0.10+7"
    if java.exists():
        env["JAVA_HOME"] = str(java)
    script_path = os.pathsep.join((str(HERE), str(SCRIPT.parent)))
    command = [home / "support/launch.bat", "fg", "jdk", "Ghidra-Headless", "8G",
               "-XX:ParallelGCThreads=2 -XX:CICompilerCount=2", "ghidra.app.util.headless.AnalyzeHeadless",
               stage, "Analysis", "-import", path, "-max-cpu", "4",
               "-scriptPath", script_path,
               "-preScript", PRE_SCRIPT.name,
               "-postScript", SCRIPT.name, stage / "program.BinExport", stage / "inventory.jsonl"]
    log = stage / "analysis.log"
    print(f"Quick analysis (not the recorded score): {path}\nLog: {log}", file=sys.stderr, flush=True)
    logged(command, log, env)
    output = log.read_text(errors="replace")
    if "KISAK_QUICK_ANALYSIS" not in output or "KISAK_BINARY_DIFF_EXPORT_COMPLETE" not in output:
        raise Blocked(f"Incomplete quick export; diagnostics: {log}")
    if any(s in output for s in ("OutOfMemoryError", "REPORT SCRIPT ERROR", "Analysis timed out")):
        raise Blocked(f"Incomplete quick export; diagnostics: {log}")
    load_inventory(stage / "inventory.jsonl")
    folder.mkdir(parents=True, exist_ok=True)
    for name in ("program.BinExport", "inventory.jsonl"):
        (folder / name).write_bytes((stage / name).read_bytes())
    manifest = {"identity": identity, "input_path": str(path), "analysis_log": str(log),
                "note": "Iteration export. Decompiler switch analysis was disabled. Not the recorded whole-EXE score."}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return folder


def filter_rows(report_path, names):
    report = json.loads(Path(report_path).read_text(encoding="utf-8"))
    wanted = set(names)
    selected = []
    seen = set()
    for row in report["summary"]["rows"]:
        candidate = leaf((row.get("candidate") or {}).get("name"))
        original = leaf(row["original"]["name"])
        matched_name = candidate if candidate in wanted else original if original in wanted else None
        if not matched_name:
            continue
        seen.add(matched_name)
        selected.append({
            "name": matched_name,
            "status": row["status"],
            "original_bytes": row["original"]["bytes"],
            "original_address": row["original"]["address"],
            "similarity_percent": row.get("similarity_percent"),
            "confidence": row.get("confidence"),
            "candidate": candidate or None,
        })
    missing = [name for name in names if name not in seen]
    return selected, missing


def main(argv=None):
    parser = argparse.ArgumentParser(description="Isolated BinDiff iteration driver")
    parser.add_argument("--candidate", default=str(ROOT / "build-release" / "KisakBlack.exe"))
    parser.add_argument("--quick", action="store_true",
                        help="Iteration score only. Skips decompiler-switch analysis when the candidate has no full export.")
    args = parser.parse_args(argv)
    candidate = Path(args.candidate).resolve()
    if not candidate.is_file():
        raise Blocked(f"Candidate executable not found: {candidate}")
    home = ghidra_home(None)
    real_export = export_binary

    def exporting(path, ghidra, refresh=False):
        resolved = Path(path).resolve()
        if args.quick and resolved == candidate and not full_export_ready(resolved, ghidra):
            print("Quick iteration export. This is not the recorded whole-EXE score.", file=sys.stderr, flush=True)
            return quick_export(resolved, ghidra)
        if args.quick and resolved == candidate:
            print("Candidate already has a full export; quick analysis was not required.", file=sys.stderr, flush=True)
        return real_export(path, ghidra, refresh)

    binary_diff.export_binary = exporting
    install_repair_cache()
    result = compare(str(candidate))
    rows, missing = filter_rows(result["json"], watch_names())
    result = {
        "status": result["status"],
        "metric": result["metric"],
        "quick_iteration_score": bool(args.quick),
        "recorded_score": "tools/re/re.ps1 binary-diff without --quick, after a full candidate export",
        "summary": result["summary"],
        "watch": rows,
        "watch_missing": missing,
        "report": result["report"],
    }
    LOCAL_WORK.mkdir(parents=True, exist_ok=True)
    out = LOCAL_WORK / "latest-watch.json"
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"status": "blocked", "reason": str(error)}, indent=2))
        raise SystemExit(2)

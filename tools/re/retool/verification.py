from __future__ import annotations

import html
import json
import struct
import time
from pathlib import Path

from . import native
from .cases import corpus, encode
from .core import (ROOT, WORK, SCHEMA, Blocked, canonical_hash, check_mapping,
                   digest, load_catalog, provenance, read_json, reference, write_json, atomic_bytes)
from .emulator import Emulator


def fixture_dir(f):
    return WORK / "captures" / f["name"]


def oracle_identity(f):
    return {"reference": load_catalog()["reference"]["sha256"], "mapping": canonical_hash(f),
            "reference_contract": digest(ROOT / "tools/re/retool/core.py"),
            "native_adapter": canonical_hash({p.name: digest(p) for p in sorted((ROOT / "tools/re/native").glob("*")) if p.is_file()}),
            "native_loader": digest(ROOT / "tools/re/retool/native.py")}


def capture(f, binary=None, exhaustive=False):
    path, pe = reference(binary)
    check_mapping(f, pe)
    cases = corpus(f["name"], exhaustive=exhaustive)
    expected = native.execute(f, cases, original=True, binary=path)
    directory = fixture_dir(f)
    identity = oracle_identity(f)
    inputs = encode(cases)
    outputs = encode(expected, width=4)
    capture_id = canonical_hash({"oracle": identity, "inputs": __import__("hashlib").sha256(inputs).hexdigest(),
                                 "outputs": __import__("hashlib").sha256(outputs).hexdigest()})
    archive = directory / capture_id
    archive.mkdir(parents=True, exist_ok=True)
    (archive / "inputs.bin").write_bytes(inputs)
    (archive / "outputs.bin").write_bytes(outputs)
    manifest = {"schema": SCHEMA, "capture_id": capture_id, "oracle": identity,
                "function": f["id"], "count": len(cases), "exhaustive": exhaustive,
                "inputs_sha256": digest(archive / "inputs.bin"), "outputs_sha256": digest(archive / "outputs.bin"),
                "origin": "native_original_instruction_region" if f["kind"] == "inline_region" else "native_original_function",
                "coverage": f["coverage"], "address_fixups": native.relocations(f, pe),
                "boundary_patch_rva": f.get("stop_rva"),
                "native_runner_sha256": digest(WORK / "native/Release/re_runner.exe")}
    write_json(archive / "manifest.json", manifest)
    write_json(directory / "latest.json", {"capture_id": capture_id})
    return {"name": f["name"], "status": "pass", "cases": len(cases), "capture": str(archive / "manifest.json")}


def load_capture(f):
    directory = fixture_dir(f)
    if not (directory / "latest.json").exists():
        raise Blocked(f"No native capture. Run capture --function {f['name']}")
    capture_id = read_json(directory / "latest.json")["capture_id"]
    if len(capture_id) != 64 or any(c not in "0123456789abcdef" for c in capture_id):
        raise Blocked("Invalid capture identity")
    archive = directory / capture_id
    manifest = read_json(archive / "manifest.json")
    if manifest.get("schema") != SCHEMA or manifest.get("oracle") != oracle_identity(f):
        raise Blocked("Stale native capture: mapping or native adapter changed; capture explicitly")
    for filename, key in (("inputs.bin", "inputs_sha256"), ("outputs.bin", "outputs_sha256")):
        if digest(archive / filename) != manifest[key]:
            raise Blocked(f"Capture integrity failure: {filename}")
    inputs = (archive / "inputs.bin").read_bytes()
    outputs = (archive / "outputs.bin").read_bytes()
    if len(inputs) != manifest["count"] * 64 or len(outputs) != manifest["count"] * 16:
        raise Blocked("Truncated native capture")
    calculated = canonical_hash({"oracle": manifest["oracle"], "inputs": manifest["inputs_sha256"], "outputs": manifest["outputs_sha256"]})
    if calculated != capture_id or manifest["capture_id"] != capture_id:
        raise Blocked("Capture manifest identity mismatch")
    return list(struct.iter_unpack("<16I", inputs)), list(struct.iter_unpack("<4I", outputs)), manifest


def first_difference(cases, expected, actual):
    if len(expected) != len(actual):
        raise Blocked("Result count mismatch")
    for i, (want, got) in enumerate(zip(expected, actual)):
        if list(want) != list(got):
            return {"index": i, "input_u32": list(cases[i]), "expected_u32": list(want), "actual_u32": list(got),
                    "first_word": next(k for k in range(4) if want[k] != got[k])}
    return None


def agreement(expected, actual):
    """Exact whole-output agreement; never partial credit for matching words."""
    if len(expected) != len(actual) or not expected:
        raise Blocked("Empty corpus or result count mismatch")
    matched = sum(list(want) == list(got) for want, got in zip(expected, actual))
    return {"matched_cases": matched, "mismatched_cases": len(expected) - matched,
            "agreement_percent": 100 * matched / len(expected)}


def accuracy_summary(rows):
    measured = [r for r in rows if r.get("status") in ("pass", "mismatch")
                and r.get("emulator_qualified") is True and "agreement_percent" in r]
    cases = sum(r["cases"] for r in measured)
    matched = sum(r["matched_cases"] for r in measured)
    return {"measured_units": len(measured), "reported_units": len(rows),
            "matched_cases": matched, "tested_cases": cases,
            "equal_unit_agreement_percent": sum(r["agreement_percent"] for r in measured) / len(measured) if measured else None,
            "pooled_case_agreement_percent": 100 * matched / cases if cases else None,
            "whole_game_accuracy_percent": None,
            "meaning": "Equal-unit mean over measured functions/inline regions only. Pooled score weights every fixture equally. Neither estimates whole-game accuracy; unknown and blocked units are excluded, never passed."}


def verify_one(f, binary=None, wasm=False):
    row = {"name": f["name"], "function": f["id"], "status": "unverified", "coverage": f["coverage"],
           "kind": f["kind"], "sources": f["sources"]}
    if f.get("mapping_status") != "verified":
        row["reason"] = f.get("blocked_reason", "Unverified binary mapping")
        return row
    try:
        path, pe = reference(binary)
        check_mapping(f, pe)
        row["provenance"] = provenance(f)
        cases, expected, manifest = load_capture(f)
        row.update(cases=len(cases), capture_id=manifest["capture_id"], backend="wasm" if wasm else "msvc-x86")
        emulator = Emulator(f, pe)
        actual = [emulator.execute(case) for case in cases]
        mismatch = first_difference(cases, expected, actual)
        row["emulator_qualified"] = mismatch is None
        if mismatch:
            row.update(status="mismatch", classification="emulator_or_fixture", mismatch=mismatch)
            return row
        actual = native.execute(f, cases, wasm=wasm)
        mismatch = first_difference(cases, expected, actual)
        row.update(agreement(expected, actual))
        row["status"] = "mismatch" if mismatch else "pass"
        if mismatch:
            row.update(classification="cpp_or_abi", mismatch=mismatch)
            # This one-case reproducer keeps bits, identities, and expected output.
            write_json(WORK / "failures" / f"{f['name']}-{'wasm' if wasm else 'native'}.json",
                       {"schema": SCHEMA, "function": f["id"], "capture_id": manifest["capture_id"],
                        "provenance": row["provenance"], **mismatch})
        return row
    except (Blocked, OSError, ValueError, KeyError) as error:
        row.update(status="blocked", reason=str(error))
        return row


def report(rows, path=None):
    default = path is None
    path = Path(path or WORK / "reports" / f"run-{time.time_ns()}.json")
    value = {"schema": SCHEMA, "created_unix": int(time.time()), "results": rows,
             "meaning": "Pass applies only to the stated corpus and coverage, not universal equivalence.",
             "accuracy": accuracy_summary(rows)}
    write_json(path, value)
    if default:
        # The stable latest snapshot is a convenience. Every caller also gets
        # its own immutable-by-convention report path, avoiding cross-agent loss.
        write_json(WORK / "reports/latest.json", value)
    sections = ["<!doctype html><meta charset=utf-8><title>Kisak binary verification</title>",
                "<style>body{font:16px system-ui;max-width:1000px;margin:40px auto;padding:0 20px}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#eee;padding:16px}article{border-top:1px solid #ccc;margin-top:24px}</style>",
                f"<h1>Kisak binary verification</h1><p>Historical observation generated at Unix time {value['created_unix']}. Rerun verification after changes. Pass applies to the recorded inputs and stated coverage. Unverified and blocked results are never passes.</p>"]
    sections += ["<h2>Measured behavioral agreement</h2><pre>" + html.escape(json.dumps(value["accuracy"], indent=2)) + "</pre>"]
    for row in rows:
        sections += [f"<article><h2>{html.escape(row['name'])}: {html.escape(row['status'])}</h2>",
                     f"<p>{html.escape(row.get('coverage', ''))}</p>",
                     "<pre>" + html.escape(json.dumps(row, indent=2)) + "</pre></article>"]
    atomic_bytes(path.with_suffix(".html"), "\n".join(sections).encode("utf-8"))
    return str(path)


def current_results(rows):
    import copy
    from .core import functions
    result = copy.deepcopy(rows)
    for row in result:
        if "provenance" not in row:
            continue
        try:
            f = functions(row["function"])[0]
            if provenance(f) != row["provenance"]:
                row.update(previous_status=row["status"], status="blocked", reason="Historical result is stale; rerun verification")
        except (Blocked, KeyError):
            row.update(previous_status=row["status"], status="blocked", reason="Mapping is no longer available")
        if row["status"] == "blocked":
            for key in ("agreement_percent", "matched_cases", "mismatched_cases"):
                row.pop(key, None)
    return result


def reproduce(fixture, binary=None, wasm=False):
    from .core import functions
    value = read_json(fixture)
    path, pe = reference(binary)
    identity = value.get("reference_sha256", value.get("provenance", {}).get("reference"))
    if identity != load_catalog()["reference"]["sha256"]:
        raise Blocked("Reproducer reference identity is missing or wrong")
    rows = []
    for record in value.get("cases", [value]):
        f = functions(record["function"])[0]
        cases, expected = [record["input_u32"]], [record["expected_u32"]]
        native_results = native.execute(f, cases, original=True, binary=path)
        emulator_results = [Emulator(f, pe).execute(cases[0])]
        if first_difference(cases, expected, native_results) or first_difference(cases, expected, emulator_results):
            raise Blocked("Reproducer's expected bits do not match both original execution paths")
        actual = native.execute(f, cases, wasm=wasm)
        mismatch = first_difference(cases, expected, actual)
        rows.append({"name": f["name"], "status": "mismatch" if mismatch else "pass",
                     "mismatch": mismatch, "coverage": f["coverage"], "provenance": provenance(f)})
    return rows

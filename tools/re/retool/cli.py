from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

from .core import (ROOT, WORK, Blocked, changed_functions, check_mapping, functions,
                   load_catalog, read_json, reference, tool_versions, write_json)


def inspect_function(f, binary=None):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    result = dict(f)
    if f.get("mapping_status") == "verified":
        path, pe = reference(binary)
        check_mapping(f, pe)
        cs = Cs(CS_ARCH_X86, CS_MODE_32)
        base = pe.OPTIONAL_HEADER.ImageBase
        result["disassembly"] = [f"{i.address:08x}  {i.mnemonic} {i.op_str}" for r in f["code"]
                                 for i in cs.disasm(pe.get_data(r["rva"], r["size"]), base + r["rva"])]
    return result


def doctor(binary=None):
    checks = []
    try:
        path, pe = reference(binary)
        checks.append({"check": "reference", "status": "pass", "path": str(path), "machine": "x86"})
    except (Blocked, OSError) as error:
        checks.append({"check": "reference", "status": "blocked", "reason": str(error)})
    for package, version in tool_versions().items():
        wanted = dict(line.strip().split("==") for line in (ROOT / "tools/re/requirements.txt").read_text().splitlines())[package]
        checks.append({"check": package, "status": "pass" if version == wanted else "blocked", "actual": version, "expected": wanted})
    from .indexer import ghidra_home
    try:
        checks.append({"check": "ghidra", "status": "pass", "path": str(ghidra_home())})
    except Blocked as error:
        checks.append({"check": "ghidra", "status": "blocked", "reason": str(error)})
    for tool in ("cmake", "node"):
        path = shutil.which(tool)
        checks.append({"check": tool, "status": "pass" if path else "blocked", "path": path})
    return checks


def parser():
    p = argparse.ArgumentParser(description="Kisak original-binary differential workflow")
    p.add_argument("--binary", help="Override original BlackOpsMP.exe path; SHA-256 must still match")
    commands = p.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor")
    d = commands.add_parser("binary-diff", help="Compare two whole x86 EXEs with Ghidra/BinDiff")
    d.add_argument("--candidate", required=True, help="Full rebuilt native executable")
    d.add_argument("--ghidra"); d.add_argument("--bindiff")
    d.add_argument("--min-confidence", type=float, default=.7)
    d.add_argument("--refresh", action="store_true", help="Reanalyze instead of reusing hash-checked exports")
    b = commands.add_parser("build"); b.add_argument("--wasm", action="store_true")
    i = commands.add_parser("index"); i.add_argument("--ghidra"); i.add_argument("--decompile-rva", type=lambda v: int(v, 0))
    w = commands.add_parser("worklist"); w.add_argument("--file")
    i = commands.add_parser("inspect"); i.add_argument("--function", required=True)
    c = commands.add_parser("capture"); c.add_argument("--function"); c.add_argument("--exhaustive", action="store_true")
    c.add_argument("--live", action="store_true"); c.add_argument("--pid", type=int); c.add_argument("--seconds", type=int, default=30)
    v = commands.add_parser("verify"); v.add_argument("--function"); v.add_argument("--file"); v.add_argument("--changed", action="store_true"); v.add_argument("--base"); v.add_argument("--wasm", action="store_true")
    r = commands.add_parser("report"); r.add_argument("--input", default=str(WORK / "reports/latest.json"))
    b = commands.add_parser("bench"); b.add_argument("--function", required=True); b.add_argument("--wasm", action="store_true")
    s = commands.add_parser("selftest"); s.add_argument("--mutation", action="store_true")
    v = commands.add_parser("live-verify"); v.add_argument("--manifest", required=True); v.add_argument("--wasm", action="store_true")
    q = commands.add_parser("search"); q.add_argument("--text"); q.add_argument("--rva", type=lambda v: int(v, 0)); q.add_argument("--caller-of", type=lambda v: int(v, 0)); q.add_argument("--limit", type=int, default=20)
    r = commands.add_parser("reproduce"); r.add_argument("--fixture", required=True); r.add_argument("--wasm", action="store_true")
    r = commands.add_parser("reconstruct"); r.add_argument("--function", required=True); r.add_argument("--compiler")
    r.add_argument("--profile", default="vc9-sse2-fast")
    r = commands.add_parser("audit-diff"); r.add_argument("--base")
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.command == "binary-diff":
            from .binary_diff import compare
            result = compare(args.candidate, args.binary, args.ghidra, args.bindiff, args.min_confidence, args.refresh)
            code = 0
        elif args.command == "doctor":
            result = doctor(args.binary)
            code = 2 if any(r["status"] != "pass" for r in result) else 0
        elif args.command == "build":
            from .native import build
            result, code = {"runner": str(build(args.wasm)), "status": "pass"}, 0
        elif args.command == "index":
            from .indexer import index
            result, code = index(args.binary, args.ghidra, args.decompile_rva), 0
        elif args.command == "worklist":
            mapped = [{"name": f["name"], "id": f["id"], "mapping": f["mapping_status"],
                       "coverage": f["coverage"], "sources": f["sources"]} for f in functions(file=args.file)]
            result = {"mapped": mapped}
            if args.file:
                from .inventory import source_worklist
                mapped_names = {r["name"] for r in mapped}
                result["unmapped_source_candidates"] = [r for r in source_worklist(args.file) if r["symbol"] not in mapped_names]
            code = 0
        elif args.command == "inspect":
            result, code = inspect_function(functions(args.function)[0], args.binary), 0
        elif args.command == "capture":
            from .verification import capture
            selected = functions(args.function)
            if args.pid is not None and not args.live:
                raise Blocked("--pid requires --live")
            if args.exhaustive and args.live:
                raise Blocked("--exhaustive applies to synthetic fixtures, not live capture")
            if args.live:
                from .live import capture_live
                result = capture_live(functions(args.function or "PM_Accelerate")[0], args.binary, args.pid, args.seconds)
                print(json.dumps(result, indent=2))
                return 0 if result["status"] == "pass" else 2
            result = []
            for f in selected:
                try:
                    result.append(capture(f, args.binary, args.exhaustive))
                except Blocked as error:
                    result.append({"name": f["name"], "status": "blocked", "reason": str(error)})
            code = 2 if any(r["status"] != "pass" for r in result) else 0
        elif args.command == "verify":
            from .verification import verify_one, report
            if sum(bool(v) for v in (args.function, args.file, args.changed)) > 1:
                raise Blocked("Choose one of --function, --file, or --changed")
            if args.base and not args.changed:
                raise Blocked("--base requires --changed")
            selected = changed_functions(args.base) if args.changed else functions(args.function, args.file)
            rows = [verify_one(f, args.binary, args.wasm) for f in selected]
            if not selected:
                rows = [{"name": args.file or "changed sources", "status": "unverified", "coverage": "none",
                         "reason": "No mapped functions selected; no behavioral coverage established"}]
            result = {"results": rows, "report": report(rows)}
            code = 1 if any(r["status"] == "mismatch" for r in rows) else 2 if any(r["status"] != "pass" for r in rows) else 0
        elif args.command == "report":
            from .verification import report, current_results
            source = Path(args.input)
            rows = current_results(read_json(source)["results"])
            checked = source.with_name(source.stem + "-checked.json")
            report(rows, checked)
            result = {"report": str(checked.with_suffix('.html')), "results": rows}
            code = 1 if any(r["status"] == "mismatch" for r in rows) else 2 if any(r["status"] != "pass" for r in rows) else 0
        elif args.command == "bench":
            from .benchmark import benchmark
            result, code = benchmark(functions(args.function)[0], args.binary, args.wasm), 0
        elif args.command == "selftest":
            from .selftest import selftest
            result = selftest(args.mutation, args.binary)
            code = 0 if result["status"] == "pass" else 1
        elif args.command == "live-verify":
            from .live import verify_live
            from .verification import report
            result = verify_live(functions("PM_Accelerate")[0], args.manifest, args.binary, args.wasm)
            report([result], WORK / "reports/live.json")
            code = 0 if result["status"] == "pass" else 1
        elif args.command == "search":
            from .inventory import search
            result, code = search(args.text, args.rva, args.caller_of, args.limit), 0
        elif args.command == "audit-diff":
            from .audit import audit_diff
            result, code = audit_diff(args.base), 0
        elif args.command == "reconstruct":
            from .codegen import reconstruct
            result = reconstruct(functions(args.function)[0], args.compiler, args.profile, args.binary)
            code = 0 if result["status"] == "pass" else 1
        elif args.command == "reproduce":
            from .verification import reproduce
            result = reproduce(args.fixture, args.binary, args.wasm)
            code = 1 if any(r["status"] == "mismatch" for r in result) else 0
        else:
            raise Blocked("Unsupported operation")
        print(json.dumps(result, indent=2))
        return code
    except Exception as error:
        print(json.dumps({"status": "blocked", "reason": str(error)}, indent=2))
        return 2

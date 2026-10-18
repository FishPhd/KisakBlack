import os
import platform
import statistics
from .core import WORK, Blocked, write_json, provenance
from .native import execute
from .verification import load_capture, verify_one, first_difference


def benchmark(f, binary=None, wasm=False):
    qualification = verify_one(f, binary, wasm)
    if qualification["status"] != "pass":
        raise Blocked("Benchmark requires passing behavioral qualification: " + qualification["status"])
    cases, expected, capture = load_capture(f)
    cases, expected = cases[:64], expected[:64]
    modes = [("cpp-wasm" if wasm else "cpp-x86", False, wasm)]
    if not wasm:
        modes.insert(0, ("original-native", True, False))
    results = []
    for name, original, use_wasm in modes:
        output, timing = execute(f, cases, original=original, binary=binary, wasm=use_wasm, benchmark=True)
        if first_difference(cases, expected, output):
            raise Blocked("Benchmark outputs no longer match the native capture")
        values = sorted(timing["adapter_ns_per_call"])
        results.append({"backend": name, "median_ns": statistics.median(values),
                        "p95_ns": values[int(.95 * (len(values) - 1))], "raw": timing})
    value = {"name": f["name"], "status": "pass", "provenance": provenance(f),
             "capture_id": capture["capture_id"], "cpu": os.environ.get("PROCESSOR_IDENTIFIER", platform.processor()),
             "os": platform.platform(), "cases": len(cases), "results": results,
             "measurement": "Warm native adapter calls, including dispatch, FP initialization and argument adaptation; excludes loading, fixture setup and I/O. No cycle or whole-game performance claim."}
    write_json(WORK / "reports" / f"bench-{f['name']}-{'wasm' if wasm else 'native'}.json", value)
    return value

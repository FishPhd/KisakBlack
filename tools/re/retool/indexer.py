import os
from pathlib import Path

from .core import ROOT, WORK, Blocked, reference, run, digest, write_json

GHIDRA_VERSION = "12.0.4"


def ghidra_home(value=None):
    path = Path(value or os.environ.get("GHIDRA_HOME", WORK / "vendor/ghidra_12.0.4_PUBLIC"))
    if not (path / "support/analyzeHeadless.bat").exists():
        raise Blocked("Ghidra 12.0.4 missing; run setup.ps1 -AnalysisTools or set GHIDRA_HOME")
    properties = (path / "Ghidra/application.properties").read_text()
    if f"application.version={GHIDRA_VERSION}" not in properties:
        raise Blocked("This workflow pins Ghidra 12.0.4; update the pin and requalify deliberately")
    return path.resolve()


def index(binary=None, ghidra=None, decompile_rva=None):
    path, pe = reference(binary)
    home = ghidra_home(ghidra)
    work = WORK / "ghidra"
    work.mkdir(parents=True, exist_ok=True)
    output = work / ("inventory.jsonl" if decompile_rva is None else f"function-{decompile_rva:x}.jsonl")
    temporary = output.with_suffix(".partial")
    env = dict(os.environ)
    env["XDG_CONFIG_HOME"] = str(WORK / "settings")
    env["XDG_CACHE_HOME"] = str(WORK / "cache")
    local_java = WORK / "vendor/jdk-21.0.10+7"
    if local_java.exists():
        env["JAVA_HOME"] = str(local_java)
    command = [home / "support/launch.bat", "fg", "jdk", "Ghidra-Headless", "8G",
               "-XX:ParallelGCThreads=2 -XX:CICompilerCount=2", "ghidra.app.util.headless.AnalyzeHeadless",
               work, "BlackOpsMP", "-scriptPath", ROOT / "tools/re/ghidra"]
    # A project is reused after the first import; no destructive overwrite.
    if (work / "BlackOpsMP.gpr").exists():
        command += ["-process", path.name]
        if (work / "index.json").exists():
            command += ["-noanalysis"]
    else:
        command += ["-import", path]
    command += ["-max-cpu", "4", "-postScript", "ExportInventory.java", temporary]
    if decompile_rva is not None:
        command.append(hex(decompile_rva))
    result = run(command, env=env, timeout=1800)
    (work / "analysis.log").write_bytes(result.stdout + result.stderr)
    if any(s in result.stdout + result.stderr for s in (b"OutOfMemoryError", b"REPORT SCRIPT ERROR", b"Analysis timed out")):
        raise Blocked("Ghidra analysis failed; inspect build-re/ghidra/analysis.log")
    if not temporary.exists():
        raise Blocked("Ghidra did not produce an inventory; inspect build-re/ghidra/analysis.log")
    import json
    with temporary.open(encoding="utf-8") as stream:
        header = json.loads(next(stream))
        if header.get("sha256", "").lower() != digest(path):
            raise Blocked("Ghidra project belongs to a different executable")
        records = [json.loads(line) for line in stream]
        count = sum(row.get("record") == "function" for row in records)
        if not records or records[-1].get("record") != "complete":
            raise Blocked("Incomplete Ghidra export")
    if not count:
        raise Blocked("Ghidra inventory has no functions")
    temporary.replace(output)
    metadata = work / ("index.json" if decompile_rva is None else f"function-{decompile_rva:x}.index.json")
    write_json(metadata, {"reference": digest(path), "tool": GHIDRA_VERSION,
                                    "functions": count, "inventory_sha256": digest(output)})
    return {"status": "pass", "functions": count, "inventory": str(output)}

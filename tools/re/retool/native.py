from pathlib import Path
import os
import sys
from functools import lru_cache
import shutil

from .core import ROOT, WORK, Blocked, canonical_hash, digest, run, write_json, read_json, reference, check_mapping, source_identity, functions
from .cases import encode, decode_results

KINDS = {"FloatPack9S": 0, "FloatPack10S": 1, "Vec2UnpackTexCoords": 2, "Byte4UnpackRgba": 3, "PM_Accelerate": 4,
         "FX_SetupVisualState": 5, "ExpandBoundsToWidth": 6, "FX_GetVelocityAtTime": 7,
         "BG_GetQuickScopeSpread": 8, "G_GetClientSize": 9}


def relocations(f, pe):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM, CS_OP_IMM
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    cs.detail = True
    base = pe.OPTIONAL_HEADER.ImageBase
    fixups = []
    for region in f["code"]:
        data = pe.get_data(region["rva"], region["size"])
        consumed = 0
        for ins in cs.disasm(data, base + region["rva"]):
            consumed += ins.size
            if (ins.group(1) or ins.group(2)) and not any(o.type == CS_OP_IMM for o in ins.operands):
                raise Blocked(f"Unsupported native indirect control flow at {ins.address:#x}")
            for operand in ins.operands:
                if operand.type == CS_OP_MEM and not operand.mem.base:
                    rva = operand.mem.disp - base
                    if ins.disp_size != 4 or not any(r["rva"] <= rva and rva + operand.size <= r["rva"] + r["size"] for r in f.get("constants", []) + f.get("globals", [])):
                        raise Blocked(f"Unsupported native absolute reference at {ins.address:#x}")
                    fixups.append(f"{ins.address - base + ins.disp_offset}:{rva}")
                if operand.type == CS_OP_IMM and (ins.group(1) or ins.group(2)):
                    target = operand.imm - base
                    if not any(r["rva"] <= target < r["rva"] + r["size"] for r in f["code"]):
                        raise Blocked(f"Native relative branch leaves declared code at {ins.address:#x}")
        if consumed != len(data):
            raise Blocked("Native code range does not end on an instruction boundary")
    return ",".join(fixups)


@lru_cache(maxsize=1)
def build_fingerprint():
    return canonical_hash({f["name"]: source_identity(f) for f in functions()})


def build(wasm=False, mutation=False):
    directory = WORK / ("mutation" if mutation else "wasm" if wasm else "native")
    source = ROOT / "tools/re/native"
    from .selection import generate
    generate(directory)
    extra = []
    if mutation:
        production = ROOT / "src/universal/com_pack.cpp"
        text = production.read_text()
        if text.count("& 0x1FF;") != 1:
            raise Blocked("Mutation anchor changed; update self-test deliberately")
        mutated = directory / "com_pack_mutated.cpp"
        mutated.write_text(text.replace("& 0x1FF;", "& 0x1FE;"), encoding="utf-8")
        extra = [f"-DKISAK_RE_PACK_SOURCE={mutated}"]
    if wasm:
        emsdk = Path(__import__("os").environ.get("EMSDK", "D:/emsdk"))
        toolchain = emsdk / "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
        if not toolchain.exists():
            raise Blocked("Wasm build needs EMSDK")
        env = dict(os.environ)
        env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")
        ninja = shutil.which("ninja")
        if not ninja:
            # Reuse this repository's established tool discovery, if configured.
            cache = ROOT / "build-web/CMakeCache.txt"
            if cache.exists():
                ninja = next((line.split("=", 1)[1] for line in cache.read_text().splitlines()
                              if line.startswith("CMAKE_MAKE_PROGRAM:FILEPATH=")), None)
        if not ninja or not Path(ninja).exists():
            raise Blocked("Wasm build needs Ninja on PATH (or an existing build-web CMake cache)")
        run(["cmake", "-S", source, "-B", directory, "-G", "Ninja",
             f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DCMAKE_BUILD_TYPE=Release"], env=env, timeout=180)
    else:
        run(["cmake", "-S", source, "-B", directory, "-G", "Visual Studio 17 2022", "-A", "Win32", *extra], timeout=180)
    result = run(["cmake", "--build", directory, "--config", "Release"], env=env if wasm else None, timeout=240)
    runner = directory / ("re_runner.js" if wasm else "Release/re_runner.exe")
    write_json(directory / "build.json", {"runner": digest(runner), "inputs": build_fingerprint(), "mutation": mutation,
               "wasm": digest(runner.with_suffix('.wasm')) if wasm else None,
               "log": result.stdout.decode(errors="replace")})
    return runner


def execute(f, cases, original=False, binary=None, wasm=False, mutation=False, benchmark=False):
    if f["name"] == "BG_GetQuickScopeSpread":
        from .cases import validate_quickscope
        for case in cases:
            validate_quickscope(case)
    directory = WORK / ("mutation" if mutation else "wasm" if wasm else "native")
    runner = directory / ("re_runner.js" if wasm else "Release/re_runner.exe")
    if not runner.exists():
        raise Blocked("Build the runner first: tools/re/re.ps1 build" + (" --wasm" if wasm else ""))
    metadata = read_json(directory / "build.json")
    if metadata.get("inputs") != build_fingerprint() or metadata.get("runner") != digest(runner):
        raise Blocked("Runner is stale or modified; run build" + (" --wasm" if wasm else ""))
    if wasm and metadata.get("wasm") != digest(runner.with_suffix('.wasm')):
        raise Blocked("Wasm runner integrity failure; rebuild")
    kind = KINDS.get(f["name"])
    if kind is None:
        raise Blocked(f"No native adapter for {f['name']}")
    command = [runner, "original" if original else "cpp", kind]
    if original:
        if wasm:
            raise Blocked("Original execution requires Windows x86")
        binary, pe = reference(binary)
        check_mapping(f, pe)
        regions = f["code"] + f.get("constants", []) + f.get("globals", [])
        if f.get("stop_rva"):
            regions = regions + [{"rva": f["stop_rva"], "size": 1}]
        command += [binary, hex(f["entry_rva"]), hex(f.get("stop_rva") or 0),
                    ",".join(f"{r['rva']}:{r['size']}" for r in regions), relocations(f, pe)]
    if wasm:
        command.insert(0, shutil.which("node") or "node")
    try:
        env = dict(os.environ)
        if benchmark:
            env["KISAK_RE_BENCH"] = "1"
        result = run(command, input=encode(cases), timeout=120, env=env)
        outputs = decode_results(result.stdout, len(cases))
        if benchmark:
            import json
            return outputs, json.loads(result.stderr.decode())
        return outputs
    except ValueError as error:
        raise Blocked(str(error)) from error

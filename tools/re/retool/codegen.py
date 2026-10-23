"""Compile production C++, then compare literal bytes after standard COFF linking.

This builds isolated function overlays at original addresses, not a replacement
game executable. Never normalize opcodes, registers, branches or relocation bytes.
"""
import hashlib
import os
import re
import struct
import time
from pathlib import Path

from .core import ROOT, WORK, Blocked, digest, image_bytes, reference, check_mapping, read_json, run, write_json, provenance
from .selection import definition


def compare_bytes(expected, actual):
    if not expected or not actual:
        raise Blocked("Empty instruction range cannot establish byte identity")
    first = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), None)
    if first is None and len(expected) != len(actual):
        first = min(len(expected), len(actual))
    return {"status": "pass" if expected == actual else "mismatch",
            "comparison": "literal_x86_bytes", "original_size": len(expected), "candidate_size": len(actual),
            "original_sha256": hashlib.sha256(expected).hexdigest(),
            "candidate_sha256": hashlib.sha256(actual).hexdigest(), "first_difference_offset": first,
            "original_at_difference": expected[first:first + 16].hex() if first is not None else None,
            "candidate_at_difference": actual[first:first + 16].hex() if first is not None else None}


class Coff:
    def __init__(self, data):
        self.data = data
        if len(data) < 20:
            raise Blocked("Truncated COFF header")
        machine, count, _, symptr, symcount, optional, _ = struct.unpack_from("<HHIIIHH", data)
        if machine != 0x14c or optional or count > 1024 or symcount > 100000:
            raise Blocked("Expected ordinary x86 COFF object")
        end = symptr + symcount * 18
        self.slice(end, 4)
        strings = self.slice(end, struct.unpack_from("<I", data, end)[0])
        def string(offset):
            if not 4 <= offset < len(strings):
                raise Blocked("Invalid COFF string offset")
            return strings[offset:].split(b"\0", 1)[0].decode("ascii")
        self.sections = []
        for i in range(count):
            row = struct.unpack("<8sIIIIIIHHI", self.slice(20 + i * 40, 40))
            name = row[0].rstrip(b"\0")
            name = string(int(name[1:])) if name.startswith(b"/") else name.decode("ascii")
            self.sections.append({"name": name, "bytes": self.slice(row[4], row[3]),
                                  "relocations": [struct.unpack("<IIH", self.slice(row[5] + j * 10, 10))
                                                  for j in range(row[7])]})
        self.symbols = {}
        i = 0
        while i < symcount:
            raw, value, section, kind, storage, aux = struct.unpack("<8sIhHBB", self.slice(symptr + i * 18, 18))
            name = string(struct.unpack_from("<I", raw, 4)[0]) if raw[:4] == bytes(4) else raw.rstrip(b"\0").decode("ascii")
            self.symbols[i] = {"name": name, "value": value, "section": section, "kind": kind, "storage": storage}
            i += 1 + aux

    def slice(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise Blocked("Truncated or invalid COFF range")
        return self.data[offset:offset + size]

    def function(self, name):
        symbols = [s for s in self.symbols.values() if s["name"].startswith("?" + name + "@@")
                   and s["section"] > 0 and s["kind"] & 0x20]
        if len(symbols) != 1:
            raise Blocked("Expected one defined COFF function symbol: " + name)
        s = symbols[0]
        section = self.sections[s["section"] - 1]
        others = [v for v in self.symbols.values() if v["section"] == s["section"] and v["kind"] & 0x20]
        if s["value"] != 0 or len(others) != 1 or not section["name"].startswith(".text"):
            raise Blocked("Function must occupy a separate /Gy code section")
        return section


def link_function(coff, name, f, pe):
    """Resolve DIR32 references to byte-checked original constant addresses only."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM
    section = coff.function(name)
    code = bytearray(section["bytes"])
    cs = Cs(CS_ARCH_X86, CS_MODE_32); cs.detail = True
    address = pe.OPTIONAL_HEADER.ImageBase + f["entry_rva"]
    instructions = list(cs.disasm(bytes(code), address))
    if sum(i.size for i in instructions) != len(code):
        raise Blocked("Candidate code contains undecodable bytes")
    fixups = []
    for offset, index, kind in section["relocations"]:
        if kind != 6 or offset + 4 > len(code) or index not in coff.symbols:
            raise Blocked("Unsupported COFF relocation; dependencies require explicit reconstruction")
        symbol = coff.symbols[index]
        ins = next((i for i in instructions if i.address - address + i.disp_offset == offset and i.disp_size == 4), None)
        if ins is None:
            raise Blocked("DIR32 relocation is not an instruction memory displacement")
        operands = [o for o in ins.operands if o.type == CS_OP_MEM and not o.mem.base]
        if len(operands) != 1:
            raise Blocked("Unsupported relocated operand")
        width = operands[0].size
        addend = struct.unpack_from("<I", code, offset)[0]
        if symbol["section"] == 0 and symbol["name"] in f.get("codegen_external_data", {}):
            target_rva = f["codegen_external_data"][symbol["name"]] + addend
            if not any(r["rva"] <= target_rva and target_rva + width <= r["rva"] + r["size"] for r in f.get("constants", [])):
                raise Blocked("External data binding leaves declared original constants")
            struct.pack_into("<I", code, offset, pe.OPTIONAL_HEADER.ImageBase + target_rva)
            fixups.append({"offset": offset, "type": "IMAGE_REL_I386_DIR32", "symbol": symbol["name"],
                           "original_rva": target_rva, "constant_hex": image_bytes(pe, target_rva, width).hex()})
            continue
        if not 1 <= symbol["section"] <= len(coff.sections):
            raise Blocked("Unresolved external COFF dependency: " + symbol["name"])
        target = coff.sections[symbol["section"] - 1]
        if not target["name"].startswith(".rdata"):
            raise Blocked("Only read-only compiler constants can bind to original addresses")
        start = symbol["value"] + addend
        value = target["bytes"][start:start + width]
        if len(value) != width:
            raise Blocked("Truncated compiler constant")
        matches = [r["rva"] for r in f.get("constants", [])
                   if r["size"] == width and image_bytes(pe, r["rva"], width) == value]
        if len(matches) != 1:
            raise Blocked("Compiler constant has no unique declared original binding: " + symbol["name"])
        struct.pack_into("<I", code, offset, pe.OPTIONAL_HEADER.ImageBase + matches[0])
        fixups.append({"offset": offset, "type": "IMAGE_REL_I386_DIR32", "symbol": symbol["name"],
                       "original_rva": matches[0], "constant_hex": value.hex()})
    return bytes(code), fixups


def reconstruct(f, compiler=None, profile="vc9-sse2-fast", binary=None):
    _, pe = reference(binary); check_mapping(f, pe)
    if f["name"] not in ("ExpandBoundsToWidth", "FX_SetupVisualState", "FX_GetVelocityAtTime", "G_GetClientSize") or len(f["code"]) != 1 or f["kind"] != "full_function":
        raise Blocked("Isolated source build supports bounds, FX and client-size units; other units need ABI/dependency bindings")
    config = read_json(ROOT / "tools/re/codegen-profiles.json")
    if profile not in config["profiles"]:
        raise Blocked("Unknown code-generation profile")
    compiler = Path(compiler or WORK / "vendor/vc9/VC/bin/cl.exe").resolve()
    if not compiler.is_file():
        raise Blocked("Local VC9 compiler missing; run tools/re/setup-codegen.ps1")
    directory = WORK / "codegen" / f["name"] / (profile + "-" + str(time.time_ns()))
    directory.mkdir(parents=True)
    source = directory / "candidate.cpp"
    header = '#include "' + (ROOT / 'tools/re/native/codegen_abi.h').as_posix() + '"\n' if f["name"].startswith("FX_") else ''
    if f["name"] == "G_GetClientSize":
        header = '#define KISAK_RE_CODEGEN_CLIENT_SIZE\n#include "' + (ROOT / 'tools/re/native/codegen_abi.h').as_posix() + '"\n'
    sources = [s for s in f["sources"] if s["symbol"] != "fx_randomTable"]
    definitions = [(s["path"], definition(ROOT / s["path"], s["symbol"])) for s in sources]
    definitions.sort(key=lambda pair: (pair[0], int(re.match(r"#line (\d+)", pair[1])[1])))
    source.write_text(header + '\n'.join(text for _, text in definitions), encoding="utf-8")
    obj = directory / "candidate.obj"
    flags = config["profiles"][profile]
    env = dict(os.environ)
    # Do not inherit CL/_CL_ or inject unknown compiler switches through the host.
    env.pop("CL", None); env.pop("_CL_", None)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    result = run([compiler, *flags, "/FAcs", "/Fa" + str(directory / "candidate.asm"),
                  "/Fo" + str(obj), source], cwd=directory, env=env, timeout=60)
    (directory / "compiler.log").write_bytes(result.stdout + result.stderr)
    code, fixups = link_function(Coff(obj.read_bytes()), f["name"], f, pe)
    original = image_bytes(pe, f["entry_rva"], f["code"][0]["size"])
    (directory / "candidate.bin").write_bytes(code)
    report = {**compare_bytes(original, code), "name": f["name"], "provenance": provenance(f),
              "compiler": str(compiler), "compiler_sha256": digest(compiler), "profile": profile,
              "compiler_components": {p.relative_to(compiler.parent).as_posix(): digest(p)
                                      for p in sorted(compiler.parent.rglob("*.dll"))},
              "flags": flags, "profile_status": config["status"], "source_sha256": digest(source),
              "object_sha256": digest(obj), "standard_link_relocations": fixups,
              "coverage": "Isolated production function at original VA with declared constant bindings; no whole-game or custom-feature link identity.",
              "artifacts": str(directory)}
    write_json(directory / "report.json", report)
    return report

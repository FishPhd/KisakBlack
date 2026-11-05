"""Compare independently mapped original functions with exported native bodies.

Export each native EXE's named bodies using ExportMultiplayerFunctions.java on
its Ghidra project after loading its adjacent PDB. This script validates every
exported instruction byte against the EXE; automatic BinDiff pairing is unused.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from mp_fidelity_common import reference, image_bytes


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode(pe, parts):
    raw, normalized, mnemonics, fixups = b"", bytearray(), [], []
    relocations = {e.rva for b in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []) for e in b.entries if e.type == 3}
    cs = Cs(CS_ARCH_X86, CS_MODE_32)
    for rva, data in parts:
        instructions = list(cs.disasm(data, pe.OPTIONAL_HEADER.ImageBase + rva))
        if sum(i.size for i in instructions) != len(data):
            raise RuntimeError("BLOCKED: undecodable code extent")
        mnemonics.extend(i.mnemonic for i in instructions)
        copy = bytearray(data)
        for address in sorted(relocations):
            if rva <= address and address + 4 <= rva + len(copy):
                offset = address - rva
                copy[offset:offset + 4] = bytes(4)
                fixups.append(len(raw) + offset)
        raw += data
        normalized.extend(copy)
    return raw, bytes(normalized), mnemonics, fixups


def byte_diff(a, b):
    return dict(equal=a == b, different_positions=sum(x != y for x, y in zip(a, b)) + abs(len(a) - len(b)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--mapping", default=Path("tests/cac_ownership_reference.json"), type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    original_path, original = reference()
    images, scopes = {}, {}
    for stage, path in (("base", args.base), ("candidate", args.candidate)):
        images[stage] = pefile.PE(str(path))
        export = json.loads(path.with_name("scoped.json").read_text())
        if export["binary_sha256"].lower() != digest(path.read_bytes()):
            raise RuntimeError("BLOCKED: scoped export identity mismatch")
        scopes[stage] = {f["name"]: f for f in export["functions"]}
        if len(scopes[stage]) != len(export["functions"]):
            raise RuntimeError("BLOCKED: duplicate native symbols")
    mapping = json.loads(args.mapping.read_text())
    rows, blocked = [], []
    for owner in mapping["functions"]:
        if not owner["scope"].startswith("complete original owner"):
            continue
        if owner.get("native_comparison_status"):
            blocked.append(dict(name=owner["name"], reason=owner["native_comparison_limit"]))
            continue
        parts = [(c["rva"], image_bytes(original, c["rva"], c["size"])) for c in owner["code"]]
        for (_, data), code in zip(parts, owner["code"]):
            if digest(data) != code["sha256"]:
                raise RuntimeError("BLOCKED: original function identity mismatch")
        a, an, ai, af = decode(original, parts)
        row = dict(name=owner["name"], original_entry_rva=owner["entry_rva"], original_bytes=len(a), original_instructions=len(ai), original_sha256=digest(a), original_highlow_offsets=af)
        for stage in images:
            f = scopes[stage][owner["name"]]
            fragments = []
            for instruction in f["instructions"]:
                rva = instruction["address"] - images[stage].OPTIONAL_HEADER.ImageBase
                data = bytes.fromhex(instruction["hex"])
                if image_bytes(images[stage], rva, len(data)) != data:
                    raise RuntimeError("BLOCKED: native export bytes differ from EXE")
                fragments.append((rva, data))
            b, bn, bi, bf = decode(images[stage], fragments)
            edits = [op for op in difflib.SequenceMatcher(None, ai, bi, autojunk=False).get_opcodes() if op[0] != "equal"]
            row[stage] = dict(entry_rva=hex(f["entry"] - images[stage].OPTIONAL_HEADER.ImageBase), bytes=len(b), instructions=len(bi), sha256=digest(b), raw=byte_diff(a, b), highlow_normalized=byte_diff(an, bn), highlow_offsets=bf, mnemonic_affected_original=sum(j-i for _, i, j, k, l in edits), mnemonic_affected_candidate=sum(l-k for _, i, j, k, l in edits))
        rows.append(row)
    report = dict(original_sha256=digest(original_path.read_bytes()), base_sha256=digest(args.base.read_bytes()), candidate_sha256=digest(args.candidate.read_bytes()), original_bytes=sum(r["original_bytes"] for r in rows), pairs=rows, blocked=blocked,
                  limits="Exact original region hashes and native named-function bodies; inline-only source mapping excluded. Raw equality includes relative addresses. Normalized comparison zeroes only PE HIGHLOW relocation operands; no opcode/register/relative-call normalization. Byte differences count positions in concatenated decoded bodies including length differences. Mnemonic edit counts ignore all operands and are diagnostics, not semantic equivalence. Ghidra-discovered bodies may omit undiscovered code. No behavioral inference from either comparison.")
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(dict(pairs=len(rows), original_bytes=report["original_bytes"], raw_equal_candidate=sum(r["candidate"]["raw"]["equal"] for r in rows))))


if __name__ == "__main__":
    main()

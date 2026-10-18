"""Bounded inventory queries keep large decompiler projects out of context."""
import json
import re
from pathlib import Path
from .core import ROOT, WORK, Blocked, digest, load_catalog, read_json


def search(text=None, rva=None, caller_of=None, limit=20):
    if not 1 <= limit <= 100:
        raise Blocked("Inventory query limit must be 1..100")
    inventory = WORK / "ghidra/inventory.jsonl"
    metadata = read_json(WORK / "ghidra/index.json")
    if metadata["reference"] != load_catalog()["reference"]["sha256"] or digest(inventory) != metadata["inventory_sha256"]:
        raise Blocked("Inventory is stale or modified; rerun index")
    result = []
    with inventory.open(encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            if row.get("record") != "function":
                continue
            if text and text.casefold() not in (row["name"] + " " + " ".join(row.get("strings", []))).casefold():
                continue
            if rva is not None and not any(r["rva"] <= rva < r["rva"] + r["size"] for r in row["ranges"]):
                continue
            if caller_of is not None and caller_of not in row["callees"]:
                continue
            row["mapping_status"] = "unverified"
            result.append(row)
            if len(result) == limit:
                break
    return result


def source_worklist(file):
    path = (ROOT / file).resolve()
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise Blocked("Worklist source must be a repository file")
    text = path.read_text(encoding="utf-8")
    # Discovery candidates only: macros and complex signatures can require
    # manual inspection. This parser never establishes binary mappings.
    pattern = r"^([\w][^\n;={}]*\b([\w:~]+)\s*\([^;{}]*\))\s*\{"
    return [{"symbol": m.group(2), "source_line": text.count("\n", 0, m.start()) + 1,
             "mapping_status": "unverified"} for m in re.finditer(pattern, text, re.M)]

from __future__ import annotations

import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "build-re"
CATALOG = ROOT / "tools/re/catalog.json"
SCHEMA = 1


class Blocked(RuntimeError):
    """Missing or unsupported evidence, never a successful comparison."""


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_json(path, value):
    atomic_bytes(path, (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8"))


def atomic_bytes(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        for attempt in range(10):
            try:
                temporary.replace(path)
                break
            except PermissionError:
                # Windows can briefly deny replacement while another writer or
                # file scanner holds the destination. Keep retries bounded.
                if attempt == 9:
                    raise
                time.sleep(.01 * (attempt + 1))
    finally:
        temporary.unlink(missing_ok=True)


def load_catalog():
    value = read_json(CATALOG)
    if value.get("schema") != SCHEMA:
        raise Blocked("Unsupported catalog schema")
    ids, names = set(), set()
    for f in value["functions"]:
        if f["id"] in ids or f["name"] in names:
            raise Blocked("Duplicate function identity or adapter name")
        ids.add(f["id"]); names.add(f["name"])
        if f.get("mapping_status") == "verified":
            if f.get("comparison") != "bitwise" or not 1 <= f.get("output_words", 0) <= 4:
                raise Blocked("Unsupported comparison or output shape")
            if not f.get("code") or any(r["rva"] < 0 or r["size"] <= 0 for r in f["code"]):
                raise Blocked("Invalid code ranges")
            if not any(r["rva"] <= f["entry_rva"] < r["rva"] + r["size"] for r in f["code"]):
                raise Blocked("Entry lies outside declared code")
            if f.get("stop_rva") and f["stop_rva"] not in [r["rva"] + r["size"] for r in f["code"]]:
                raise Blocked("Inline stop must be an explicit code boundary")
    return value


def binary_path(value=None):
    return Path(value or os.environ.get("KISAK_REFERENCE", ROOT / "build/BlackOpsMP.exe")).resolve()


def reference(path=None):
    import pefile
    require_tool_versions()
    path = binary_path(path)
    catalog = load_catalog()
    if not path.is_file():
        raise Blocked(f"Reference not found: {path}")
    identity = digest(path)
    if identity != catalog["reference"]["sha256"]:
        raise Blocked(f"Wrong reference SHA-256: {identity}; expected {catalog['reference']['sha256']}")
    pe = pefile.PE(str(path))
    if pe.FILE_HEADER.Machine != 0x14C:
        raise Blocked("Reference must be x86 PE32")
    return path, pe


def functions(selection=None, file=None):
    all_functions = load_catalog()["functions"]
    if selection:
        matches = [f for f in all_functions if selection in (f["name"], f["id"])]
        if not matches:
            raise Blocked(f"Unknown function: {selection}")
        return matches
    if file:
        normalized = str(file).replace("\\", "/")
        return [f for f in all_functions if any(s["path"] == normalized for s in f["sources"])]
    return all_functions


def check_mapping(f, pe):
    if f.get("mapping_status") != "verified":
        raise Blocked(f"{f['name']}: mapping is unverified")
    base = pe.OPTIONAL_HEADER.ImageBase
    for region in f["code"] + f.get("constants", []) + f.get("globals", []):
        raw = image_bytes(pe, region["rva"], region["size"])
        if len(raw) != region["size"] or hashlib.sha256(raw).hexdigest() != region["sha256"]:
            raise Blocked(f"{f['name']}: stale bytes at {base + region['rva']:#x}")


def image_bytes(pe, rva, size):
    """PE memory includes zero-filled BSS which has no corresponding file bytes."""
    raw = pe.get_data(rva, size)
    if len(raw) == size:
        return raw
    section = pe.get_section_by_rva(rva) if hasattr(pe, "get_section_by_rva") else None
    if section is not None and rva + size <= section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData):
        return raw.ljust(size, b"\0")
    return raw


def tool_versions():
    versions = {}
    for package in ("unicorn", "capstone", "pefile", "frida"):
        try:
            versions[package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            versions[package] = None
    return versions


def require_tool_versions():
    wanted = dict(line.strip().split("==") for line in (ROOT / "tools/re/requirements.txt").read_text().splitlines())
    for package, version in tool_versions().items():
        if wanted.get(package) != version:
            raise Blocked(f"Dependency version mismatch for {package}: {version}; run setup.ps1")


def source_identity(f):
    # All headers can affect ABI or inline helpers. Conservative invalidation is
    # intentional; hashing only the named .cpp would miss dependency changes.
    files = {ROOT / s["path"] for s in f["sources"]}
    files.add(ROOT / "src/qcommon/msg_mp.cpp")
    files.add(ROOT / "tools/re/codegen-profiles.json")
    files.update(p for root in (ROOT / "src", ROOT / "tl") for p in root.rglob("*")
                 if p.suffix in (".h", ".hpp", ".inl", ".inc"))
    files.update((ROOT / "tools/re").rglob("*.py"))
    files.update((ROOT / "tools/re/native").rglob("*.*"))
    return canonical_hash({p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files) if p.is_file()})


def provenance(f):
    return {"reference": load_catalog()["reference"]["sha256"], "function": f["id"],
            "mapping": canonical_hash(f), "source": source_identity(f), "tools": tool_versions()}


def run(command, **kwargs):
    try:
        result = subprocess.run([str(x) for x in command], capture_output=True, **kwargs)
    except subprocess.TimeoutExpired as error:
        raise Blocked(f"Command exceeded its time limit: {command[0]}") from error
    if result.returncode:
        detail = result.stderr or result.stdout
        if isinstance(detail, bytes):
            detail = detail.decode(errors="replace")
        raise Blocked(f"Command failed ({result.returncode}): {command[0]}\n{detail[-6000:]}")
    return result


def changed_functions(base=None):
    commands = [["git", "diff", "--name-only", base or "HEAD"],
                ["git", "ls-files", "--others", "--exclude-standard"]]
    names = set()
    for command in commands:
        names.update(run(command, cwd=ROOT, text=True).stdout.splitlines())
    if any(p.endswith((".h", ".hpp")) or p.startswith("tools/re/") or
           p.startswith("cmake/") or p in ("CMakeLists.txt", "cmake_files.cmake") for p in names):
        return functions()
    selected = {f["name"] for f in functions() if any(s["path"] in names for s in f["sources"])}
    while True:
        additions = {f["name"] for f in functions() if selected.intersection(f.get("depends_on", []))}
        if additions <= selected:
            break
        selected.update(additions)
    return [f for f in functions() if f["name"] in selected]

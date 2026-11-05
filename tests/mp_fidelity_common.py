"""Small, fail-closed helpers for the standalone multiplayer fidelity checks."""
import hashlib
import importlib.metadata
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SHA256 = "17304fa6bf0eac57ac8da34c3d30b9106e6cc0d0263b0ce189b9bcd8c34675cc"


def reference():
    import pefile
    for package, version in {"pefile": "2024.8.26", "capstone": "5.0.6", "unicorn": "2.1.4"}.items():
        if importlib.metadata.version(package) != version:
            raise RuntimeError(f"BLOCKED: {package} requires {version}")
    path = Path(os.environ.get("KISAK_REFERENCE", ROOT / "build/BlackOpsMP.exe")).resolve()
    if hashlib.sha256(path.read_bytes()).hexdigest() != REFERENCE_SHA256:
        raise RuntimeError("BLOCKED: original executable identity mismatch")
    pe = pefile.PE(str(path))
    if pe.FILE_HEADER.Machine != 0x14c:
        raise RuntimeError("BLOCKED: original must be x86 PE32")
    return path, pe


def image_bytes(pe, rva, size):
    raw = pe.get_data(rva, size)
    section = pe.get_section_by_rva(rva)
    if len(raw) != size and section is not None and rva + size <= section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData):
        raw = raw.ljust(size, b"\0")
    if len(raw) != size:
        raise RuntimeError("BLOCKED: incomplete original region")
    return raw


def _mask(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: " " * len(m.group()), text, flags=re.S)


def read_source(path):
    raw = Path(path).read_bytes()
    for encoding in ("utf-8-sig", "cp1252"):
        try:
            return raw.decode(encoding).replace("\r\n", "\n").replace("\r", "\n")
        except UnicodeDecodeError:
            pass
    raise RuntimeError("BLOCKED: unsupported production source encoding")


def _body(path, text, masked, match, data=False):
    start, position, depth = match.start(), match.end(), 1
    while depth and position < len(masked):
        depth += (masked[position] == "{") - (masked[position] == "}")
        position += 1
    if depth:
        raise RuntimeError("BLOCKED: unterminated production definition")
    line = text.count("\n", 0, start) + 1
    return f'#line {line} "{path.as_posix()}"\n' + text[start:position] + (";\n" if data else "\n")


def definition(path, symbol, data=False):
    path = Path(path)
    text = read_source(path)
    masked = _mask(text)
    suffix = r"\[[^\]]*\]\s*=\s*\{" if data else r"\([^;{}]*\)\s*\{"
    matches = [m for m in re.finditer(r"^(?P<prefix>[^\n;{}()]*)\b" + re.escape(symbol) + suffix, masked, re.M)
               if re.fullmatch(r"[A-Za-z_][\w\s:*&<>]*", m["prefix"].strip())]
    if len(matches) != 1:
        raise RuntimeError(f"BLOCKED: expected one production definition of {symbol}")
    return _body(path, text, masked, matches[0], data)


def exact_definition(path, declaration):
    path = Path(path)
    text = read_source(path)
    matches = list(re.finditer(r"^" + re.escape(declaration) + r"\s*\{", text, re.M))
    if len(matches) != 1:
        raise RuntimeError(f"BLOCKED: expected one exact declaration of {declaration}")
    return _body(path, text, _mask(text), matches[0])

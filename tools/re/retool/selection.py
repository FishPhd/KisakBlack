"""Generate selected production definitions without maintaining copied logic."""
import re
from .core import ROOT, Blocked, digest, write_json


def definition(path, symbol, data=False):
    text = path.read_text(encoding="utf-8")
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group()), text, flags=re.S)
    suffix = r"\[[^\]]*\]\s*=\s*\{" if data else r"\([^;{}]*\)\s*\{"
    matches = list(re.finditer(r"^[^\n;{}]*\b" + re.escape(symbol) + suffix, masked, re.M))
    if len(matches) != 1:
        raise Blocked(f"Expected one production definition of {symbol}, found {len(matches)}")
    start, position, depth = matches[0].start(), matches[0].end(), 1
    while depth and position < len(masked):
        depth += (masked[position] == "{") - (masked[position] == "}")
        position += 1
    if depth:
        raise Blocked(f"Unterminated definition of {symbol}")
    line = text.count("\n", 0, start) + 1
    return f'#line {line} "{path.as_posix()}"\n' + text[start:position] + (";\n" if data else "\n")


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    paths = [(ROOT / "src/bgame/bg_pmove.cpp", "PM_Accelerate"),
             (ROOT / "src/universal/com_math.cpp", "Vec3Normalize")]
    header = '#include <bgame/bg_local.h>\n#include <bgame/bg_pmove.h>\n#include <bgame/bg_misc.h>\n'
    selected = directory / "selected.cpp"
    fields = ROOT / "src/qcommon/msg_mp.cpp"
    arrays = re.findall(r"^const NetField (\w+)\[", fields.read_text(encoding="utf-8"), re.M)
    selected.write_text(header + "\n".join(definition(p, s) for p, s in paths) +
                       "\n".join(definition(fields, symbol, data=True) for symbol in arrays), encoding="utf-8")
    write_json(directory / "selected.json", {"production": {str(p.relative_to(ROOT)): digest(p) for p, _ in paths},
                                          "generated": digest(selected)})

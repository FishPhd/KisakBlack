"""Select the actual production event/dvar path for private verification fixtures."""
import re

from .core import ROOT, Blocked
from .selection import definition


def _exact_definition(path, declaration, template_prefix=None):
    """Keep an overloaded declaration (and optional template line) verbatim."""
    text = path.read_text(encoding="utf-8")
    matches = list(re.finditer(r"^" + re.escape(declaration) + r"\s*\{", text, re.M))
    if len(matches) != 1:
        raise Blocked(f"Expected one exact production declaration {declaration}, found {len(matches)}")
    match = matches[0]
    start = match.start()
    if template_prefix:
        prefix = text[:start]
        prior = re.search(re.escape(template_prefix) + r"\s*$", prefix)
        if not prior:
            raise Blocked(f"Missing production template prefix for {declaration}")
        start = prior.start()
    # Match braces only after masking literals/comments, just as the shared
    # selector does; declarations remain exact production text, not copies.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group()), text, flags=re.S)
    position, depth = match.end(), 1
    while depth and position < len(masked):
        depth += (masked[position] == "{") - (masked[position] == "}")
        position += 1
    if depth:
        raise Blocked(f"Unterminated production event dependency {declaration}")
    line = text.count("\n", 0, start) + 1
    return f'#line {line} "{path.as_posix()}"\n' + text[start:position] + "\n"


def definitions():
    """No replacement algorithms or false-return dvar doubles are generated."""
    dvar = ROOT / "src/universal/dvar.cpp"
    misc = ROOT / "src/bgame/bg_misc.cpp"
    header = """#include <win32/win_common.h>
#include <win32/win_net.h>
#include <intrin.h>
#include <ctype.h>
extern FastCriticalSection g_dvarCritSect;
extern dvar_s* dvarHashTable[1024];
dvar_s* __cdecl Dvar_FindMalleableVar_0(int);
int __cdecl Com_HashString(const char*, int);
"""
    parts = [header]
    for symbol in ("Dvar_FindMalleableVar_0", "Sys_UnlockRead", "Dvar_FindMalleableVar",
                   "Dvar_GetBool", "Dvar_StringToBool"):
        parts.append(definition(dvar, symbol))
    parts.append(_exact_definition(dvar, "const dvar_s *__cdecl Dvar_FindVar(const char *dvarName)"))
    parts.append(definition(ROOT / "src/server_mp/sv_bot_mp.cpp", "Com_HashString"))
    parts.append(definition(misc, "eventnames", data=True))
    parts.append(_exact_definition(
        misc,
        "void BG_AddEvent(\n    unsigned int newEvent,\n    unsigned int eventParm,\n"
        "    __int16 *eventSequence,\n    EventType *events,\n    EventParmType *eventParms,\n"
        "    unsigned int eventTime,\n    char addType)",
        template_prefix="template<typename EventType, typename EventParmType>"))
    parts.append(definition(misc, "BG_AddPredictableEventToPlayerstate"))
    parts.append(definition(ROOT / "src/bgame/bg_pmove.cpp", "PM_AddEvent"))
    return "\n".join(parts)


def sources():
    """The catalog should include these dependencies in source/change identity."""
    return [{"path": "src/universal/dvar.cpp", "symbol": name} for name in (
        "Dvar_FindMalleableVar_0", "Sys_UnlockRead", "Dvar_FindMalleableVar", "Dvar_FindVar",
        "Dvar_GetBool", "Dvar_StringToBool")] + [
        {"path": "src/server_mp/sv_bot_mp.cpp", "symbol": "Com_HashString"},
        {"path": "src/bgame/bg_misc.cpp", "symbol": "eventnames"},
        {"path": "src/bgame/bg_misc.cpp", "symbol": "BG_AddEvent"},
        {"path": "src/bgame/bg_misc.cpp", "symbol": "BG_AddPredictableEventToPlayerstate"},
        {"path": "src/bgame/bg_pmove.cpp", "symbol": "PM_AddEvent"}]

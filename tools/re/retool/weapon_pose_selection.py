"""Select actual mantle/animation dependencies without replacing their behavior."""
import re

from .core import ROOT, Blocked
from .selection import definition


def definitions():
    mantle = ROOT / "src/bgame/bg_mantle.cpp"
    source = mantle.read_text(encoding="utf-8")
    structs = list(re.finditer(r"^struct MantleAnimTransition\b[^{}]*\{[^{}]*\};", source, re.M))
    if len(structs) != 1:
        raise Blocked("Expected one production MantleAnimTransition structure")
    match = structs[0]
    line = source.count("\n", 0, match.start()) + 1
    parts = ['#include <bgame/bg_mantle.h>\n#include <bgame/bg_perks.h>\n#include <xanim/xanim.h>\n'
             'extern const dvar_t* mantle_enable;\nextern XAnim_s* s_mantleAnims;\n',
             f'#line {line} "{mantle.as_posix()}"\n' + match.group(),
             definition(mantle, "s_mantleTrans", data=True)]
    parts.extend(definition(mantle, name) for name in (
        "Mantle_IsWeaponInactive", "Mantle_IsWeaponEquipped", "Trans_GetUpIndex",
        "Trans_GetOverIndex", "Mantle_GetUpLength", "Mantle_GetOverLength",
        "Mantle_GetDuration", "Mantle_GetMantleFrac"))
    animation = ROOT / "src/xanim/xanim.cpp"
    parts.extend(definition(animation, name) for name in ("IsLeafNode", "XAnimGetLength", "XAnimGetLengthMsec"))
    return "\n".join(parts)

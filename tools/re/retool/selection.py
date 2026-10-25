"""Generate selected production definitions without maintaining copied logic."""
import re
from .core import ROOT, Blocked, digest, write_json


def definition(path, symbol, data=False):
    text = path.read_text(encoding="utf-8")
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group()), text, flags=re.S)
    suffix = r"\[[^\]]*\]\s*=\s*\{" if data else r"\([^;{}]*\)\s*\{"
    # A declaration has a nonempty type/storage prefix. Calls inside if/while
    # conditions must not be mistaken for a definition ending at that brace.
    matches = [m for m in re.finditer(r"^(?P<prefix>[^\n;{}()]*)\b" + re.escape(symbol) + suffix, masked, re.M)
               if re.fullmatch(r"[A-Za-z_][\w\s:*&<>]*", m["prefix"].strip())]
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
             (ROOT / "src/universal/com_math.cpp", "Vec3Normalize"),
             (ROOT / "src/universal/com_math.cpp", "ExpandBoundsToWidth"),
             (ROOT / "src/universal/com_math.cpp", "DiffTrack"),
             (ROOT / "src/EffectsCore/fx_draw.cpp", "FX_SetupVisualState"),
             (ROOT / "src/EffectsCore/fx_update_util.cpp", "FX_GetVelocityAtTime"),
             (ROOT / "src/EffectsCore/fx_update_util.cpp", "FX_GetVelocityAtTimeInFrame"),
             (ROOT / "src/EffectsCore/fx_update_util.cpp", "FX_OrientationDirToWorldDir"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_GetQuickScopeSpread"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_GetViewmodelWeaponIndex"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_IsUseAsMeleeWeapon"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_StartWeaponAnim"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_ContinueWeaponAnim"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_GetHeldWeapon"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_PlayerWeaponSetNeedsRechamber"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_Weapon_FinishRechamber"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_Weapon_CheckForRechamber"),
             (ROOT / "src/bgame/bg_weapons.cpp", "BG_PlayerWeaponNeedsRechambered"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_ExitAimDownSight"),
             (ROOT / "src/bgame/bg_weapons.cpp", "PM_Weapon_OffHandInit"),
             (ROOT / "src/bgame/bg_weapons_view.cpp", "BG_CalculateWeaponMovement_GetLowReadyLerp"),
             (ROOT / "src/bgame/bg_weapons_view.cpp", "BG_CalculateWeaponMovement_GetDTPLerp"),
             (ROOT / "src/bgame/bg_misc.cpp", "BG_GetHeldWeaponConst"),
             (ROOT / "src/bgame/bg_misc.cpp", "BG_GetHeldWeaponSlot"),
             (ROOT / "src/bgame/bg_weapons_def.cpp", "BG_GetWeaponDef"),
             (ROOT / "src/game_mp/g_main_mp.cpp", "G_GetClientSize")]
    header = '#include <bgame/bg_weapons_def.h>\n#include <bgame/bg_local.h>\n#include <bgame/bg_pmove.h>\n#include <bgame/bg_misc.h>\n#include <bgame/bg_weapons.h>\n#include <game_mp/g_main_mp.h>\n#include <EffectsCore/fx_draw.h>\n#include <EffectsCore/fx_update_util.h>\n#include <EffectsCore/fx_random.h>\n#include <qcommon/common.h>\n'
    header += '#include <bgame/bg_weapons_view.h>\n#include <cgame/cg_weapons.h>\n'
    selected = directory / "selected.cpp"
    fields = ROOT / "src/qcommon/msg_mp.cpp"
    arrays = re.findall(r"^const NetField (\w+)\[", fields.read_text(encoding="utf-8"), re.M)
    from .event_selection import definitions as event_definitions
    from .offhand_selection import definitions as offhand_definitions
    from .offhand_hold_selection import definitions as hold_definitions
    from .game_debug_selection import definitions as debug_definitions
    from .melee_end_selection import definitions as melee_definitions
    selected.write_text(header + "\n".join(definition(p, s) for p, s in paths) +
                       event_definitions() +
                       offhand_definitions() +
                       hold_definitions() + debug_definitions() +
                       melee_definitions() +
                       "\n".join(definition(fields, symbol, data=True) for symbol in arrays) +
                       definition(ROOT / "src/EffectsCore/fx_random.cpp", "fx_randomTable", data=True) +
                       '\nfloat re_quickscope_spread(const playerState_s* ps) { return BG_GetQuickScopeSpread(ps); }\n', encoding="utf-8")
    write_json(directory / "selected.json", {"production": {str(p.relative_to(ROOT)): digest(p) for p, _ in paths},
                                          "generated": digest(selected)})

"""Additional actual production definitions for complete melee completion."""
from .core import ROOT
from .selection import definition

SOURCES = [
    ("src/bgame/bg_weapons.cpp", "PM_Weapon_MeleeEnd"),
    ("src/bgame/bg_weapons.cpp", "PM_Weapon_GetMeleeWeapDef"),
    ("src/bgame/bg_weapons.cpp", "PM_Weapon_Idle"),
    ("src/bgame/bg_pmove.cpp", "PM_SetProneMovementOverride"),
]


def definitions():
    # BG_GetWeaponDef, BG_IsUseAsMeleeWeapon and PM_StartWeaponAnim are already
    # selected by the existing weapon units. The real game diagnostic dependency
    # is selected once by game_debug_selection with a disabled private fixture.
    return "\n".join(definition(ROOT / path, symbol) for path, symbol in SOURCES)

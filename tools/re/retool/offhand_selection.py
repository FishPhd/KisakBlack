"""Additional real production definitions for the bounded offhand-end unit."""
from .core import ROOT
from .selection import definition

SOURCES = [
    ("src/bgame/bg_weapons.cpp", "PM_Weapon_OffHandEnd"),
    ("src/bgame/bg_weapons_ammo.cpp", "PM_WeaponClipEmpty"),
    ("src/bgame/bg_mantle.cpp", "BG_GetAmmoInClip"),
    ("src/bgame/bg_mantle.cpp", "BG_GetAmmoInClipForWeaponDef"),
    ("src/bgame/bg_weapons_def.cpp", "BG_GetWeaponVariantDef"),
]


def definitions():
    return ("#include <bgame/bg_weapons_ammo.h>\n#include <bgame/bg_mantle.h>\n" +
            "\n".join(definition(ROOT / path, symbol) for path, symbol in SOURCES))

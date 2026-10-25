"""Actual production definitions for original offhand-hold qualification."""
from .core import ROOT
from .selection import definition

SOURCES = [("src/bgame/bg_weapons.cpp", "PM_Weapon_OffHandHold"),
           ("src/bgame/bg_weapons.cpp", "BG_ThrowingBackGrenade")]


def definitions():
    return "\n".join(definition(ROOT / path, symbol) for path, symbol in SOURCES)

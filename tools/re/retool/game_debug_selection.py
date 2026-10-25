"""Select the real diagnostic gate, with disabled settings supplied as fixtures."""
from .core import ROOT
from .selection import definition

SOURCES = [("src/game_mp/g_main_mp.cpp", "G_IsServerGameSystem")]


def definitions():
    return ("#include <bgame/bg_animation.h>\n#include <game_mp/g_main_mp.h>\n" +
            "\n".join(definition(ROOT / path, symbol) for path, symbol in SOURCES))

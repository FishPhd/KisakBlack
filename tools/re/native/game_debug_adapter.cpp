// Private bindings for the actual production diagnostic gate. Its disabled
// setting is an input fixture, not a replacement return value.
#include <bgame/bg_local.h>
#include <bgame/bg_animation.h>
#include <game_mp/g_main_mp.h>
#include <cstdlib>

const dvar_t* g_debugPlayerAnimScript = nullptr;
thread_local bgs_t* bgs = nullptr;
bgs_t level_bgs{};
level_locals_t level{};

void re_game_debug_reset()
{
    g_debugPlayerAnimScript = nullptr;
    bgs = nullptr;
}

void re_game_debug_check()
{
    if (g_debugPlayerAnimScript || bgs) std::abort();
}

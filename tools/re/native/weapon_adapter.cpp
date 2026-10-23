#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <game_mp/g_main_mp.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

extern float re_quickscope_spread(const playerState_s*);
extern int __cdecl G_GetClientSize();
const dvar_s* bg_aqs = nullptr;
const dvar_s* player_aqs_spread_max = nullptr;
const dvar_s* player_aqs_time = nullptr;
const dvar_s* player_aqs_spread_multiplier = nullptr;

static_assert(offsetof(playerState_s, aimSpreadScale) == 0x560);
static_assert(offsetof(playerState_s, quickScopeScale) == 0x4ec);
static_assert(offsetof(playerState_s, quickScopeTimer) == 0x4f0);
static_assert(offsetof(dvar_s, current) == 0x18);
static_assert(sizeof(gclient_s) == 0x2a28, "Client-size codegen binding must match production");

void weapon_call(int kind, bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (kind == 9)
    {
#ifdef _WIN32
        if (original) output[0] = reinterpret_cast<int(__cdecl*)()>(entry)();
        else
#endif
        output[0] = G_GetClientSize();
        return;
    }
    playerState_s ps{};
    std::memcpy(&ps.aimSpreadScale, input + 1, 4);
    std::memcpy(&ps.quickScopeScale, input + 2, 4);
    std::memcpy(&ps.quickScopeTimer, input + 3, 4);
    dvar_s settings[4]{};
    settings[0].current.enabled = input[0] != 0;
    for (unsigned i = 1; i < 4; ++i)
        std::memcpy(&settings[i].current.value, input + i + 3, 4);
    const playerState_s before = ps;
    dvar_s before_settings[4]; std::memcpy(before_settings, settings, sizeof(settings));
    bg_aqs = settings;
    player_aqs_spread_max = settings + 1;
    player_aqs_time = settings + 2;
    player_aqs_spread_multiplier = settings + 3;
    float result;
#ifdef _WIN32
    if (original)
    {
        auto image = static_cast<unsigned char*>(entry) - 0x3bd940;
        const unsigned globals[] = {0x84f874, 0x84f928, 0x851a8c, 0x8519e0};
        for (unsigned i = 0; i < 4; ++i)
        {
            DWORD protection, ignored;
            if (!VirtualProtect(image + globals[i], 4, PAGE_READWRITE, &protection)) std::abort();
            *reinterpret_cast<dvar_s**>(image + globals[i]) = settings + i;
            if (!VirtualProtect(image + globals[i], 4, protection, &ignored)) std::abort();
        }
        auto state = &ps;
        __asm {
            mov eax, state
            call entry
            movss result, xmm0
        }
    }
    else
#endif
    result = re_quickscope_spread(&ps);
    std::memcpy(output, &result, 4);
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(before_settings, settings, sizeof(settings))) std::abort();
    bg_aqs = player_aqs_spread_max = player_aqs_time = player_aqs_spread_multiplier = nullptr;
}

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
WeaponVariantDef* bg_weaponVariantDefs[2048]{};
unsigned int bg_lastParsedWeaponIndex = 2047;

static_assert(offsetof(playerState_s, aimSpreadScale) == 0x560);
static_assert(offsetof(playerState_s, quickScopeScale) == 0x4ec);
static_assert(offsetof(playerState_s, quickScopeTimer) == 0x4f0);
static_assert(offsetof(dvar_s, current) == 0x18);
static_assert(sizeof(gclient_s) == 0x2a28, "Client-size codegen binding must match production");
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, offHandIndex) == 0x134);
static_assert(offsetof(playerState_s, weapon) == 0x144 && sizeof(playerState_s::weapon) == 2);
static_assert(offsetof(playerState_s, meleeWeapon) == 0x154);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, bHasBayonet) == 0x569);
static_assert(offsetof(WeaponDef, bUseAsMelee) == 0x64c);

static void viewmodel_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[2] >= 2048 || !input[3] || input[5] > 1 || input[6] > 1) std::abort();
    playerState_s ps{};
    std::memcpy(&ps.weaponstate, input, 4);
    std::memcpy(&ps.weapFlags, input + 1, 4);
    ps.weapon = static_cast<unsigned short>(input[2]);
    std::memcpy(&ps.offHandIndex, input + 3, 4);
    std::memcpy(&ps.meleeWeapon, input + 4, 4);
    WeaponDef definition{};
    definition.bHasBayonet = input[5] != 0;
    definition.bUseAsMelee = input[6] != 0;
    WeaponVariantDef variant{};
    variant.weapDef = &definition;
    bg_weaponVariantDefs[input[2]] = &variant;
    const playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x2b94f0 + 0x880258;
        DWORD protection, ignored;
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &protection)) std::abort();
        std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), protection, &ignored)) std::abort();
        output[0] = reinterpret_cast<unsigned(__cdecl*)(const playerState_s*)>(entry)(&ps);
        if (std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs))) std::abort();
    }
    else
#endif
    output[0] = BG_GetViewmodelWeaponIndex(&ps);
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_variant, &variant, sizeof(variant))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[2] ? &variant : nullptr)) std::abort();
    bg_weaponVariantDefs[input[2]] = nullptr;
}

void weapon_call(int kind, bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (kind == 11)
    {
        viewmodel_call(original, entry, input, output);
        return;
    }
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

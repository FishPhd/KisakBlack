#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

extern WeaponVariantDef* bg_weaponVariantDefs[2048];
extern void re_game_debug_reset();
extern void re_game_debug_check();
static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, weaponTimeLeft) == 0x44);
static_assert(offsetof(playerState_s, weaponDelayLeft) == 0x48);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, meleeWeapon) == 0x154);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weaponstateLeft) == 0x15c);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);
static_assert(offsetof(playerState_s, waterlevel) == 0x590);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, bHasBayonet) == 0x569);
static_assert(offsetof(WeaponDef, bUseAsMelee) == 0x64c);
static_assert(offsetof(WeaponDef, quickRaiseTime) == 0x408);

// current weapon, melee weapon, shared-definition bool, bayonet, use-as-melee,
// pm_type, waterlevel, pm_flags, weapFlags, quickRaiseTime, uint16 right/left
// animations, left time/delay/state, output view (0/1). Each fixture has both views.
void melee_end_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[0] >= 2048 || input[1] >= 2048 || input[2] > 1 || input[3] > 1 ||
        input[4] > 1 || input[10] > 65535 || input[11] > 65535 || input[15] > 1) std::abort();
    re_game_debug_reset();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    ps.meleeWeapon = input[1];
    std::memcpy(&ps.pm_type, input + 5, 4);
    std::memcpy(&ps.waterlevel, input + 6, 4);
    std::memcpy(&ps.pm_flags, input + 7, 4);
    std::memcpy(&ps.weapFlags, input + 8, 4);
    std::memcpy(&ps.weapAnim, input + 10, 4);
    std::memcpy(&ps.weapAnimLeft, input + 11, 4);
    std::memcpy(&ps.weaponTimeLeft, input + 12, 4);
    std::memcpy(&ps.weaponDelayLeft, input + 13, 4);
    std::memcpy(&ps.weaponstateLeft, input + 14, 4);
    ps.weaponstate = static_cast<weaponstate_t>(0x12345678);
    ps.weaponTime = -0x1234567;
    ps.weaponDelay = 0x12345678;
    WeaponDef definitions[2]{};
    definitions[0].bHasBayonet = input[3] != 0;
    definitions[0].bUseAsMelee = input[4] != 0;
    std::memcpy(&definitions[0].quickRaiseTime, input + 9, 4);
    WeaponVariantDef variants[2]{};
    variants[0].weapDef = definitions;
    variants[1].weapDef = definitions + (input[2] ? 0 : 1);
    bg_weaponVariantDefs[input[0]] = variants;
    if (input[1] != input[0]) bg_weaponVariantDefs[input[1]] = variants + 1;
    playerState_s before = ps;
    WeaponDef before_definitions[2]; std::memcpy(before_definitions, definitions, sizeof(definitions));
    WeaponVariantDef before_variants[2]; std::memcpy(before_variants, variants, sizeof(variants));
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3c1710 + 0x880258;
        DWORD protection, ignored;
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &protection)) std::abort();
        std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), protection, &ignored)) std::abort();
        auto state = &ps;
        __asm {
            push esi
            mov esi, state
            call entry
            pop esi
        }
        if (std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs))) std::abort();
    }
    else
#endif
    PM_Weapon_MeleeEnd(&ps);
    re_game_debug_check();
    // Both original paths store zero delay; animation inputs/results fit uint16.
    if (ps.weaponDelay || static_cast<unsigned>(ps.weapAnim) > 65535 ||
        static_cast<unsigned>(ps.weapAnimLeft) > 65535) std::abort();
    if (!input[15])
    {
        std::memcpy(output, &ps.weaponstate, 4);
        std::memcpy(output + 1, &ps.weaponTime, 4);
        std::memcpy(output + 2, &ps.pm_flags, 4);
        output[3] = static_cast<unsigned>(ps.weapAnim) | (static_cast<unsigned>(ps.weapAnimLeft) << 16);
    }
    else
    {
        std::memcpy(output, &ps.weapFlags, 4);
        std::memcpy(output + 1, &ps.weaponTimeLeft, 4);
        std::memcpy(output + 2, &ps.weaponDelayLeft, 4);
        std::memcpy(output + 3, &ps.weaponstateLeft, 4);
    }
    before.weaponstate = ps.weaponstate;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.pm_flags = ps.pm_flags;
    before.weapFlags = ps.weapFlags;
    before.weapAnim = ps.weapAnim;
    before.weapAnimLeft = ps.weapAnimLeft;
    before.weaponTimeLeft = ps.weaponTimeLeft;
    before.weaponDelayLeft = ps.weaponDelayLeft;
    before.weaponstateLeft = ps.weaponstateLeft;
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(before_definitions, definitions, sizeof(definitions)) ||
        std::memcmp(before_variants, variants, sizeof(variants))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? variants : i == input[1] ? variants + 1 : nullptr))
            std::abort();
    bg_weaponVariantDefs[input[0]] = bg_weaponVariantDefs[input[1]] = nullptr;
}

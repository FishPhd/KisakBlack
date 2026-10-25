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
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, grenadeTimeLeft) == 0x50);
static_assert(offsetof(playerState_s, throwBackGrenadeOwner) == 0x54);
static_assert(offsetof(playerState_s, offHandIndex) == 0x134);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, fuseTime) == 0x46c);

// Eight fixture words: offhand index, weapon flags, throwback owner, grenade
// timer, asset fuse time, initial state, initial weaponTime, initial delay.
void offhand_hold_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (!input[0] || input[0] >= 2048) std::abort();
    re_game_debug_reset();
    playerState_s ps{};
    std::memcpy(&ps.offHandIndex, input, 4);
    std::memcpy(&ps.weapFlags, input + 1, 4);
    std::memcpy(&ps.throwBackGrenadeOwner, input + 2, 4);
    std::memcpy(&ps.grenadeTimeLeft, input + 3, 4);
    std::memcpy(&ps.weaponstate, input + 5, 4);
    std::memcpy(&ps.weaponTime, input + 6, 4);
    std::memcpy(&ps.weaponDelay, input + 7, 4);
    WeaponDef definition{};
    std::memcpy(&definition.fuseTime, input + 4, 4);
    WeaponVariantDef variant{}; variant.weapDef = &definition;
    bg_weaponVariantDefs[input[0]] = &variant;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3c1a60 + 0x880258;
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
    PM_Weapon_OffHandHold(&ps);
    re_game_debug_check();
    // Original 0x7c1a7a writes zero to weaponDelay; an unexpected value blocks.
    if (ps.weaponDelay != 0) std::abort();
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.weapFlags, 4);
    std::memcpy(output + 2, &ps.grenadeTimeLeft, 4);
    std::memcpy(output + 3, &ps.weaponTime, 4);
    before.weaponstate = ps.weaponstate;
    before.weapFlags = ps.weapFlags;
    before.grenadeTimeLeft = ps.grenadeTimeLeft;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_variant, &variant, sizeof(variant))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? &variant : nullptr)) std::abort();
    bg_weaponVariantDefs[input[0]] = nullptr;
}

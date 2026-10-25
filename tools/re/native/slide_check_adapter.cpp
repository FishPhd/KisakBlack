#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cgame_mp/cg_predict_mp.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

extern WeaponVariantDef* bg_weaponVariantDefs[2048];
static_assert(sizeof(playerState_s) == 0x26e8);
static_assert(sizeof(WeaponDef) == 0x808);
static_assert(sizeof(WeaponVariantDef) == 0xe4);
static_assert(offsetof(pmove_t, ps) == 0);
static_assert(offsetof(pmove_t, cmd) + offsetof(usercmd_s, serverTime) == 4);
static_assert(offsetof(pmove_t, cmd) + offsetof(usercmd_s, weapon) == 0x1c);
static_assert(sizeof(usercmd_s::weapon) == 2);
static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, bRunLeftGun) == 0x61);
static_assert(offsetof(playerState_s, slideTime) == 0xac);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);
static_assert(offsetof(playerState_s, waterlevel) == 0x590);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, slideInTime) == 0x448);

// current weapon, state/water/pm_flags/slideTime/cmd.serverTime/pm_type,
// right/left animations, duration, weaponTime/delay, weapFlags, left state,
// cmd.weapon (uint16) | run-left<<16, left weaponTime.
void slide_check_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (!input[0] || input[0] >= 2048 || input[14] > 0x1ffff) std::abort();
    for (auto binding : bg_weaponVariantDefs)
        if (binding) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.weaponstate, input + 1, 4);
    std::memcpy(&ps.waterlevel, input + 2, 4);
    std::memcpy(&ps.pm_flags, input + 3, 4);
    std::memcpy(&ps.slideTime, input + 4, 4);
    std::memcpy(&ps.pm_type, input + 6, 4);
    std::memcpy(&ps.weapAnim, input + 7, 4);
    std::memcpy(&ps.weapAnimLeft, input + 8, 4);
    std::memcpy(&ps.weaponTime, input + 10, 4);
    std::memcpy(&ps.weaponDelay, input + 11, 4);
    std::memcpy(&ps.weapFlags, input + 12, 4);
    std::memcpy(&ps.weaponstateLeft, input + 13, 4);
    ps.bRunLeftGun = (input[14] >> 16) != 0;
    std::memcpy(&ps.weaponTimeLeft, input + 15, 4);
    // Construct the actual production type so virtual proximity-data state has
    // a valid lifetime. Only the declared first three pmove fields are read by
    // this routine; preserve the complete constructed object representation.
    pmove_t movement{};
    movement.ps = &ps;
    std::memcpy(&movement.cmd.serverTime, input + 5, 4);
    movement.cmd.weapon = static_cast<unsigned short>(input[14] & 0xffff);
    unsigned char before_movement[sizeof(movement)];
    std::memcpy(before_movement, &movement, sizeof(movement));
    WeaponDef definition{};
    std::memcpy(&definition.slideInTime, input + 9, 4);
    WeaponVariantDef variant{}; variant.weapDef = &definition;
    bg_weaponVariantDefs[input[0]] = &variant;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3bedb0 + 0x880258;
        DWORD protection, ignored;
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &protection)) std::abort();
        std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), protection, &ignored)) std::abort();
        auto pm = &movement;
        __asm {
            mov ecx, pm
            call entry
        }
        if (std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs))) std::abort();
    }
    else
#endif
        PM_Weapon_CheckForSlide(&movement);
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.weaponTime, 4);
    std::memcpy(output + 2, &ps.weaponDelay, 4);
    std::memcpy(output + 3, &ps.weapAnim, 4);
    before.weaponstate = ps.weaponstate;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.weapAnim = ps.weapAnim;
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(before_movement, &movement, sizeof(movement)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_variant, &variant, sizeof(variant))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? &variant : nullptr)) std::abort();
    bg_weaponVariantDefs[input[0]] = nullptr;
}

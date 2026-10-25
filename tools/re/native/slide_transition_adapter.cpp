#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
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
static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, bRunLeftGun) == 0x61);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, slideInTime) == 0x448);

// weapon, pm_type, right/left animation, run-left, asset duration,
// state/time/delay, weapFlags, pm_flags, left state/time/delay, water, reserved0.
void slide_transition_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (!input[0] || input[0] >= 2048 || input[4] > 1 || input[15]) std::abort();
    for (auto binding : bg_weaponVariantDefs)
        if (binding) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.pm_type, input + 1, 4);
    std::memcpy(&ps.weapAnim, input + 2, 4);
    std::memcpy(&ps.weapAnimLeft, input + 3, 4);
    ps.bRunLeftGun = input[4] != 0;
    std::memcpy(&ps.weaponstate, input + 6, 4);
    std::memcpy(&ps.weaponTime, input + 7, 4);
    std::memcpy(&ps.weaponDelay, input + 8, 4);
    std::memcpy(&ps.weapFlags, input + 9, 4);
    std::memcpy(&ps.pm_flags, input + 10, 4);
    std::memcpy(&ps.weaponstateLeft, input + 11, 4);
    std::memcpy(&ps.weaponTimeLeft, input + 12, 4);
    std::memcpy(&ps.weaponDelayLeft, input + 13, 4);
    std::memcpy(&ps.waterlevel, input + 14, 4);
    WeaponDef definition{};
    std::memcpy(&definition.slideInTime, input + 5, 4);
    WeaponVariantDef variant{}; variant.weapDef = &definition;
    bg_weaponVariantDefs[input[0]] = &variant;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3be840 + 0x880258;
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
        Slide_State_In(&ps);
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.weaponTime, 4);
    std::memcpy(output + 2, &ps.weaponDelay, 4);
    std::memcpy(output + 3, &ps.weapAnim, 4);
    before.weaponstate = ps.weaponstate;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.weapAnim = ps.weapAnim;
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_variant, &variant, sizeof(variant))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? &variant : nullptr)) std::abort();
    bg_weaponVariantDefs[input[0]] = nullptr;
}

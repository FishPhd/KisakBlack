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
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, contFireInTime) == 0x430);
static_assert(offsetof(WeaponDef, contFireOutTime) == 0x438);
static_assert(offsetof(WeaponDef, deployTime) == 0x44c);
static_assert(offsetof(WeaponDef, breakdownTime) == 0x450);

// weapon, pm_type, right animation, asset duration, initial state/time/delay,
// weapFlags, pm_flags, left animation/state/time/delay, grenade time, waterlevel,
// reserved0. Only state/time/delay/right animation may change.
void timed_weapon_call(int kind, bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (kind < 24 || kind > 28 || !input[0] || input[0] >= 2048 || input[15]) std::abort();
    for (auto binding : bg_weaponVariantDefs)
        if (binding) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.pm_type, input + 1, 4);
    std::memcpy(&ps.weapAnim, input + 2, 4);
    std::memcpy(&ps.weaponstate, input + 4, 4);
    std::memcpy(&ps.weaponTime, input + 5, 4);
    std::memcpy(&ps.weaponDelay, input + 6, 4);
    std::memcpy(&ps.weapFlags, input + 7, 4);
    std::memcpy(&ps.pm_flags, input + 8, 4);
    std::memcpy(&ps.weapAnimLeft, input + 9, 4);
    std::memcpy(&ps.weaponstateLeft, input + 10, 4);
    std::memcpy(&ps.weaponTimeLeft, input + 11, 4);
    std::memcpy(&ps.weaponDelayLeft, input + 12, 4);
    std::memcpy(&ps.grenadeTimeLeft, input + 13, 4);
    std::memcpy(&ps.waterlevel, input + 14, 4);
    const unsigned duration_offsets[] = {0x430, 0, 0x438, 0x44c, 0x450};
    const unsigned entry_rvas[] = {0x3be610, 0x3be660, 0x3be690, 0x3beb80, 0x3bebd0};
    const unsigned duration_offset = duration_offsets[kind - 24];
    WeaponDef definition{};
    WeaponVariantDef variant{};
    if (duration_offset)
    {
        std::memcpy(reinterpret_cast<unsigned char*>(&definition) + duration_offset, input + 3, 4);
        variant.weapDef = &definition;
        bg_weaponVariantDefs[input[0]] = &variant;
    }
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
#ifdef _WIN32
    if (original)
    {
        void* table = nullptr;
        if (duration_offset)
        {
            table = static_cast<unsigned char*>(entry) - entry_rvas[kind - 24] + 0x880258;
            DWORD protection, ignored;
            if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &protection)) std::abort();
            std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
            if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), protection, &ignored)) std::abort();
        }
        auto state = &ps;
        if (kind == 25)
        {
            __asm {
                mov eax, state
                call entry
            }
        }
        else
        {
            __asm {
                push esi
                mov esi, state
                call entry
                pop esi
            }
        }
        if (table && std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs))) std::abort();
    }
    else
#endif
    {
        switch (kind)
        {
        case 24: ContinuousFire_State_In(&ps); break;
        case 25: ContinuousFire_State_Loop(&ps); break;
        case 26: ContinuousFire_State_Out(&ps); break;
        case 27: Deploy_State_Deploy(&ps); break;
        case 28: Deploy_State_Breakdown(&ps); break;
        default: std::abort();
        }
    }
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
        if (bg_weaponVariantDefs[i] != (duration_offset && i == input[0] ? &variant : nullptr)) std::abort();
    if (duration_offset) bg_weaponVariantDefs[input[0]] = nullptr;
}

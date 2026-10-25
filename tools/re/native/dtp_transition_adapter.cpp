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
static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, bRunLeftGun) == 0x61);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weaponstateLeft) == 0x15c);
static_assert(offsetof(playerState_s, ammoInClip) == 0x3f4);
static_assert(sizeof(AmmoClip) == 8);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);
static_assert(offsetof(WeaponDef, unlimitedAmmo) == 0x358);
static_assert(offsetof(WeaponDef, bDualWield) == 0x56a);
static_assert(offsetof(WeaponDef, dualWieldWeaponIndex) == 0x594);
static_assert(offsetof(WeaponDef, dtpInTime) == 0x43c);
static_assert(offsetof(WeaponDef, dtpOutTime) == 0x444);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponVariantDef, iClipIndex) == 0x4c);

// weapon, pm_type, right/left animation, config(run-left1/dual2/unlimited4/view8),
// right/left counts, right/left slots, duration, initial right/left states,
// time, delay, weapon flags, movement flags. Paired views observe all six words.
void dtp_transition_call(int kind, bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (kind < 29 || kind > 31 || !input[0] || input[0] >= 2048 || input[4] > 15 ||
        input[7] > 15 || input[8] > 15 || (input[7] && input[7] == input[8])) std::abort();
    for (auto binding : bg_weaponVariantDefs)
        if (binding) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.pm_type, input + 1, 4);
    std::memcpy(&ps.weapAnim, input + 2, 4);
    std::memcpy(&ps.weapAnimLeft, input + 3, 4);
    ps.bRunLeftGun = (input[4] & 1) != 0;
    std::memcpy(&ps.weaponstate, input + 10, 4);
    std::memcpy(&ps.weaponstateLeft, input + 11, 4);
    std::memcpy(&ps.weaponTime, input + 12, 4);
    std::memcpy(&ps.weaponDelay, input + 13, 4);
    std::memcpy(&ps.weapFlags, input + 14, 4);
    std::memcpy(&ps.pm_flags, input + 15, 4);
    for (unsigned i = 0; i < 15; ++i)
    {
        ps.ammoInClip[i].clipIndex = 0x10000 + i;
        ps.ammoInClip[i].count = 0x3456789;
    }
    if (input[7])
    {
        ps.ammoInClip[input[7] - 1].clipIndex = 123;
        std::memcpy(&ps.ammoInClip[input[7] - 1].count, input + 5, 4);
    }
    if (input[8])
    {
        ps.ammoInClip[input[8] - 1].clipIndex = 456;
        std::memcpy(&ps.ammoInClip[input[8] - 1].count, input + 6, 4);
    }
    const unsigned left_weapon = input[0] == 2047 ? 1 : 2047;
    WeaponDef definition{};
    definition.unlimitedAmmo = (input[4] & 4) != 0;
    definition.bDualWield = (input[4] & 2) != 0;
    definition.dualWieldWeaponIndex = left_weapon;
    if (kind == 29) std::memcpy(&definition.dtpInTime, input + 9, 4);
    if (kind == 31) std::memcpy(&definition.dtpOutTime, input + 9, 4);
    WeaponVariantDef variants[2]{};
    variants[0].weapDef = variants[1].weapDef = &definition;
    variants[0].iClipIndex = 123;
    variants[1].iClipIndex = 456;
    bg_weaponVariantDefs[input[0]] = variants;
    bg_weaponVariantDefs[left_weapon] = variants + 1;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    WeaponVariantDef before_variants[2]; std::memcpy(before_variants, variants, sizeof(variants));
#ifdef _WIN32
    if (original)
    {
        const unsigned entry_rvas[] = {0x3be6e0, 0x3bea20, 0x3be8c0};
        auto table = static_cast<unsigned char*>(entry) - entry_rvas[kind - 29] + 0x880258;
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
    {
        switch (kind)
        {
        case 29: Dtp_State_In(&ps); break;
        case 30: Dtp_State_Loop(&ps); break;
        case 31: Dtp_State_Out(&ps); break;
        default: std::abort();
        }
    }
    if (input[4] & 8)
    {
        std::memcpy(output, &ps.weapAnim, 4);
        std::memcpy(output + 1, &ps.weapAnimLeft, 4);
        std::memcpy(output + 2, &ps.weaponstate, 4);
        std::memcpy(output + 3, &ps.weaponstateLeft, 4);
    }
    else
    {
        std::memcpy(output, &ps.weaponstate, 4);
        std::memcpy(output + 1, &ps.weaponstateLeft, 4);
        std::memcpy(output + 2, &ps.weaponTime, 4);
        std::memcpy(output + 3, &ps.weaponDelay, 4);
    }
    before.weaponstate = ps.weaponstate;
    before.weaponstateLeft = ps.weaponstateLeft;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.weapAnim = ps.weapAnim;
    before.weapAnimLeft = ps.weapAnimLeft;
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(before_variants, variants, sizeof(variants))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? variants : i == left_weapon ? variants + 1 : nullptr))
            std::abort();
    bg_weaponVariantDefs[input[0]] = bg_weaponVariantDefs[left_weapon] = nullptr;
}

#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

extern WeaponVariantDef* bg_weaponVariantDefs[2048];

static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, throwBackGrenadeOwner) == 0x54);
static_assert(offsetof(playerState_s, throwBackGrenadeTimeLeft) == 0x58);
static_assert(offsetof(playerState_s, bRunLeftGun) == 0x61);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, ammoInClip) == 0x3f4);
static_assert(sizeof(AmmoClip) == 8);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);
static_assert(offsetof(WeaponDef, unlimitedAmmo) == 0x358);
static_assert(offsetof(WeaponDef, quickRaiseTime) == 0x408);
static_assert(offsetof(WeaponDef, bDualWield) == 0x56a);
static_assert(offsetof(WeaponDef, dualWieldWeaponIndex) == 0x594);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponVariantDef, iClipIndex) == 0x4c);

// Fixture words: weapon, weapFlags, pm_flags, pm_type, right anim, left anim,
// run-left, dual-wield, unlimited-ammo, quickRaiseTime, right count, left count,
// right slot+1 (0 means absent), left slot+1, throwback owner, throwback time.
// Counts/times are signed raw words. Animation inputs must fit uint16.
void offhand_end_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (!input[0] || input[0] >= 2048 || input[4] > 65535 || input[5] > 65535 ||
        input[6] > 1 || input[7] > 1 || input[8] > 1 || input[12] > 15 || input[13] > 15 ||
        (input[12] && input[12] == input[13])) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.weapFlags, input + 1, 4);
    std::memcpy(&ps.pm_flags, input + 2, 4);
    std::memcpy(&ps.pm_type, input + 3, 4);
    std::memcpy(&ps.weapAnim, input + 4, 4);
    std::memcpy(&ps.weapAnimLeft, input + 5, 4);
    ps.bRunLeftGun = input[6] != 0;
    std::memcpy(&ps.throwBackGrenadeOwner, input + 14, 4);
    std::memcpy(&ps.throwBackGrenadeTimeLeft, input + 15, 4);
    ps.weaponDelay = 0x12345678;
    ps.weaponTime = -0x1234567;
    ps.weaponstate = static_cast<weaponstate_t>(0x12345678);
    for (unsigned i = 0; i < 15; ++i)
    {
        ps.ammoInClip[i].clipIndex = 0x10000 + i;
        ps.ammoInClip[i].count = 0x3456789;
    }
    if (input[12])
    {
        ps.ammoInClip[input[12] - 1].clipIndex = 123;
        std::memcpy(&ps.ammoInClip[input[12] - 1].count, input + 10, 4);
    }
    if (input[13])
    {
        ps.ammoInClip[input[13] - 1].clipIndex = 456;
        std::memcpy(&ps.ammoInClip[input[13] - 1].count, input + 11, 4);
    }
    // A distinct second weapon lets left-hand clip selection vary independently.
    const unsigned left_weapon = input[0] == 2047 ? 1 : 2047;
    WeaponDef definition{};
    definition.unlimitedAmmo = input[8] != 0;
    definition.bDualWield = input[7] != 0;
    definition.dualWieldWeaponIndex = left_weapon;
    std::memcpy(&definition.quickRaiseTime, input + 9, 4);
    WeaponVariantDef variants[2]{};
    variants[0].weapDef = variants[1].weapDef = &definition;
    variants[0].iClipIndex = 123;
    variants[1].iClipIndex = 456;
    bg_weaponVariantDefs[input[0]] = variants;
    bg_weaponVariantDefs[left_weapon] = variants + 1;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    WeaponVariantDef before_variants[2];
    std::memcpy(before_variants, variants, sizeof(variants));
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3be200 + 0x880258;
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
    PM_Weapon_OffHandEnd(&ps);
    // These fixed postconditions come directly from 0x7be249 and 0x7be2b9..
    // 0x7be2ce. Violations block a run; they never synthesize expected outputs.
    if (ps.weaponDelay != 0 || ps.throwBackGrenadeTimeLeft != 0 ||
        ps.throwBackGrenadeOwner != 1023 || ps.pm_flags != (before.pm_flags & ~0x200u) ||
        static_cast<unsigned>(ps.weapAnim) > 65535 || static_cast<unsigned>(ps.weapAnimLeft) > 65535)
        std::abort();
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.weapFlags, 4);
    std::memcpy(output + 2, &ps.weaponTime, 4);
    output[3] = static_cast<unsigned>(ps.weapAnim) | (static_cast<unsigned>(ps.weapAnimLeft) << 16);
    before.weaponstate = ps.weaponstate;
    before.weapFlags = ps.weapFlags;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.pm_flags = ps.pm_flags;
    before.throwBackGrenadeOwner = ps.throwBackGrenadeOwner;
    before.throwBackGrenadeTimeLeft = ps.throwBackGrenadeTimeLeft;
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

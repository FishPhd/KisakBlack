#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

extern WeaponVariantDef* bg_weaponVariantDefs[2048];
extern void re_event_reset();
extern void re_event_check();
static_assert(sizeof(playerState_s) == 0x26e8);
static_assert(sizeof(WeaponDef) == 0x808);
static_assert(sizeof(WeaponVariantDef) == 0xe4);
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, predictableEventSequence) == 0xe8);
static_assert(offsetof(playerState_s, predictableEvents) == 0xec);
static_assert(offsetof(playerState_s, predictableEventParms) == 0xfc);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, meleeWeapon) == 0x154);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(WeaponVariantDef, weapDef) == 8);
static_assert(offsetof(WeaponDef, bHasBayonet) == 0x569);
static_assert(offsetof(WeaponDef, bUseAsMelee) == 0x64c);

// current weapon, melee weapon, bool bayonet/use-as-melee, pm_flags, initial
// state, sequence byte, event/parameter seeds, weaponTime, weaponDelay,
// weapFlags, commandTime, pm_type, waterlevel, output view0/1. Both views run
// each fixture: state/pm_flags/sequence/event, then state/pm_flags/sequence/parm.
void melee_fire_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[0] >= 2048 || input[1] >= 2048 || input[2] > 1 || input[3] > 1 ||
        input[6] > 255 || input[15] > 1) std::abort();
    re_event_reset();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    ps.meleeWeapon = input[1];
    std::memcpy(&ps.pm_flags, input + 4, 4);
    std::memcpy(&ps.weaponstate, input + 5, 4);
    ps.predictableEventSequence = static_cast<short>(input[6]);
    for (unsigned i = 0; i < 4; ++i)
    {
        const unsigned event = input[7] ^ (0x1020304u * (i + 1));
        std::memcpy(ps.predictableEvents + i, &event, 4);
        ps.predictableEventParms[i] = input[8] ^ (0x4030201u * (i + 1));
    }
    std::memcpy(&ps.weaponTime, input + 9, 4);
    std::memcpy(&ps.weaponDelay, input + 10, 4);
    std::memcpy(&ps.weapFlags, input + 11, 4);
    std::memcpy(&ps.commandTime, input + 12, 4);
    std::memcpy(&ps.pm_type, input + 13, 4);
    std::memcpy(&ps.waterlevel, input + 14, 4);
    WeaponDef definitions[2]{};
    definitions[0].bHasBayonet = input[2] != 0;
    definitions[0].bUseAsMelee = input[3] != 0;
    WeaponVariantDef variants[2]{};
    variants[0].weapDef = definitions;
    variants[1].weapDef = definitions + 1;
    bg_weaponVariantDefs[input[0]] = variants;
    if (input[1] != input[0]) bg_weaponVariantDefs[input[1]] = variants + 1;
    playerState_s before = ps;
    WeaponDef before_definitions[2]; std::memcpy(before_definitions, definitions, sizeof(definitions));
    WeaponVariantDef before_variants[2]; std::memcpy(before_variants, variants, sizeof(variants));
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3bdfd0 + 0x880258;
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
        PM_Weapon_MeleeFire(&ps);
    re_event_check();
    const unsigned slot = input[6] & 3;
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.pm_flags, 4);
    output[2] = static_cast<unsigned short>(ps.predictableEventSequence);
    if (input[15]) std::memcpy(output + 3, ps.predictableEventParms + slot, 4);
    else std::memcpy(output + 3, ps.predictableEvents + slot, 4);
    before.weaponstate = ps.weaponstate;
    before.pm_flags = ps.pm_flags;
    before.predictableEventSequence = ps.predictableEventSequence;
    before.predictableEvents[slot] = ps.predictableEvents[slot];
    before.predictableEventParms[slot] = ps.predictableEventParms[slot];
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(before_definitions, definitions, sizeof(definitions)) ||
        std::memcmp(before_variants, variants, sizeof(variants))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? variants : i == input[1] ? variants + 1 : nullptr))
            std::abort();
    bg_weaponVariantDefs[input[0]] = bg_weaponVariantDefs[input[1]] = nullptr;
}

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
static_assert(offsetof(WeaponDef, bBoltAction) == 0x54f);
static_assert(offsetof(WeaponDef, iRechamberTime) == 0x3b4);
static_assert(offsetof(WeaponDef, iRechamberBoltTime) == 0x3b8);
static_assert(offsetof(playerState_s, fWeaponPosFrac) == 0x168);
static_assert(offsetof(playerState_s, predictableEventSequence) == 0xe8);
static_assert(offsetof(playerState_s, predictableEvents) == 0xec);
static_assert(offsetof(playerState_s, predictableEventParms) == 0xfc);

void rechamber_check_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[0] >= 2048 || input[1] > 15 || input[2] > 0x7fff ||
        input[9] > 1 || input[10] > 1 || input[15] > 0x1ff) std::abort();
    re_event_reset();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.weaponstate, input + 3, 4);
    std::memcpy(&ps.weaponTime, input + 4, 4);
    std::memcpy(&ps.weaponDelay, input + 5, 4);
    std::memcpy(&ps.pm_type, input + 6, 4);
    std::memcpy(&ps.weapAnim, input + 7, 4);
    std::memcpy(&ps.weapAnimLeft, input + 8, 4);
    ps.bRunLeftGun = input[9] != 0;
    std::memcpy(&ps.fWeaponPosFrac, input + 13, 4);
    ps.predictableEventSequence = static_cast<short>(input[15] & 255);
    for (unsigned i = 0; i < 4; ++i) ps.predictableEvents[i] = 0x55;
    for (unsigned i = 0; i < 15; ++i)
    {
        ps.heldWeapons[i].weapon = input[1] == i + 1 ? input[0] : 0x10000 + i;
        ps.heldWeapons[i].needsRechamber = (input[2] & (1u << i)) != 0;
    }
    WeaponDef definition{};
    definition.bBoltAction = input[10] != 0;
    std::memcpy(&definition.iRechamberTime, input + 11, 4);
    std::memcpy(&definition.iRechamberBoltTime, input + 12, 4);
    WeaponVariantDef variant{}; variant.weapDef = &definition;
    bg_weaponVariantDefs[input[0]] = &variant;
    playerState_s before = ps;
    const WeaponDef before_definition = definition;
    const WeaponVariantDef before_variant = variant;
    int result;
#ifdef _WIN32
    if (original)
    {
        auto table = static_cast<unsigned char*>(entry) - 0x3bcd60 + 0x880258;
        DWORD protection, ignored;
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &protection)) std::abort();
        std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), protection, &ignored)) std::abort();
        auto state = &ps;
        const unsigned delayed = input[14];
        __asm {
            push delayed
            mov eax, state
            call entry
            add esp, 4
            mov result, eax
        }
        if (std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs))) std::abort();
    }
    else
#endif
    result = PM_Weapon_CheckForRechamber(&ps, static_cast<int>(input[14]));
    re_event_check();
    const unsigned event_slot = input[15] & 3;
    unsigned needs = 0;
    for (unsigned i = 0; i < 15; ++i)
    {
        needs |= unsigned(ps.heldWeapons[i].needsRechamber) << i;
        before.heldWeapons[i].needsRechamber = ps.heldWeapons[i].needsRechamber;
    }
    if (input[15] & 256)
    {
        std::memcpy(output, &result, 4);
        output[1] = needs;
        output[2] = static_cast<unsigned short>(ps.predictableEventSequence);
        std::memcpy(output + 3, ps.predictableEvents + event_slot, 4);
    }
    else
    {
        std::memcpy(output, &ps.weaponstate, 4);
        std::memcpy(output + 1, &ps.weaponTime, 4);
        std::memcpy(output + 2, &ps.weaponDelay, 4);
        std::memcpy(output + 3, &ps.weapAnim, 4);
    }
    before.weaponstate = ps.weaponstate;
    before.weaponTime = ps.weaponTime;
    before.weaponDelay = ps.weaponDelay;
    before.weapAnim = ps.weapAnim;
    before.predictableEventSequence = ps.predictableEventSequence;
    before.predictableEvents[event_slot] = ps.predictableEvents[event_slot];
    if (std::memcmp(&before, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_variant, &variant, sizeof(variant))) std::abort();
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i == input[0] ? &variant : nullptr)) std::abort();
    bg_weaponVariantDefs[input[0]] = nullptr;
}

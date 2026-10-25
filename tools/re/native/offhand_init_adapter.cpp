#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

extern void re_event_reset();
extern void re_event_check();
static_assert(offsetof(playerState_s, pm_flags) == 0xc);
static_assert(offsetof(playerState_s, weapFlags) == 0x10);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, weaponDelay) == 0x40);
static_assert(offsetof(playerState_s, grenadeTimeLeft) == 0x50);
static_assert(offsetof(playerState_s, throwBackGrenadeOwner) == 0x54);
static_assert(offsetof(playerState_s, throwBackGrenadeTimeLeft) == 0x58);
static_assert(offsetof(playerState_s, predictableEventSequence) == 0xe8);
static_assert(offsetof(playerState_s, predictableEvents) == 0xec);
static_assert(offsetof(playerState_s, predictableEventParms) == 0xfc);
static_assert(offsetof(playerState_s, offHandIndex) == 0x134);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, waterlevel) == 0x590);

// offhand index, uint16 current weapon, signed-raw waterlevel, weapon flags,
// pm_flags, state, delay, owner, sequence byte, event seed, parameter seed,
// weaponTime, grenade timer, throwback timer, commandTime, output view (0/1).
// Every logical fixture is replayed with both views to observe all eight words.
void offhand_init_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (!input[0] || input[0] >= 2048 || input[1] > 65535 || input[8] > 255 || input[15] > 1)
        std::abort();
    re_event_reset();
    playerState_s ps{};
    std::memcpy(&ps.offHandIndex, input, 4);
    ps.weapon = static_cast<unsigned short>(input[1]);
    std::memcpy(&ps.waterlevel, input + 2, 4);
    std::memcpy(&ps.weapFlags, input + 3, 4);
    std::memcpy(&ps.pm_flags, input + 4, 4);
    std::memcpy(&ps.weaponstate, input + 5, 4);
    std::memcpy(&ps.weaponDelay, input + 6, 4);
    std::memcpy(&ps.throwBackGrenadeOwner, input + 7, 4);
    ps.predictableEventSequence = static_cast<short>(input[8]);
    for (unsigned i = 0; i < 4; ++i)
    {
        const unsigned event = input[9] ^ (0x1020304u * (i + 1));
        std::memcpy(ps.predictableEvents + i, &event, 4);
        ps.predictableEventParms[i] = input[10] ^ (0x4030201u * (i + 1));
    }
    std::memcpy(&ps.weaponTime, input + 11, 4);
    std::memcpy(&ps.grenadeTimeLeft, input + 12, 4);
    std::memcpy(&ps.throwBackGrenadeTimeLeft, input + 13, 4);
    std::memcpy(&ps.commandTime, input + 14, 4);
    playerState_s before = ps;
#ifdef _WIN32
    if (original)
    {
        auto state = &ps;
        __asm {
            push esi
            mov esi, state
            call entry
            pop esi
        }
    }
    else
#endif
    PM_Weapon_OffHandInit(&ps);
    re_event_check();
    const unsigned slot = input[8] & 3;
    if (input[15])
    {
        std::memcpy(output, &ps.throwBackGrenadeOwner, 4);
        output[1] = static_cast<unsigned short>(ps.predictableEventSequence);
        std::memcpy(output + 2, ps.predictableEvents + slot, 4);
        std::memcpy(output + 3, ps.predictableEventParms + slot, 4);
    }
    else
    {
        std::memcpy(output, &ps.weaponstate, 4);
        std::memcpy(output + 1, &ps.weapFlags, 4);
        std::memcpy(output + 2, &ps.weaponDelay, 4);
        std::memcpy(output + 3, &ps.pm_flags, 4);
    }
    if (before.waterlevel < 3)
    {
        before.weaponstate = ps.weaponstate;
        before.weapFlags = ps.weapFlags;
        before.weaponDelay = ps.weaponDelay;
        before.pm_flags = ps.pm_flags;
        before.throwBackGrenadeOwner = ps.throwBackGrenadeOwner;
        before.predictableEventSequence = ps.predictableEventSequence;
        before.predictableEvents[slot] = ps.predictableEvents[slot];
        before.predictableEventParms[slot] = ps.predictableEventParms[slot];
    }
    if (std::memcmp(&before, &ps, sizeof(ps))) std::abort();
}

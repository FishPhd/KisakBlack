#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

static_assert(offsetof(playerState_s, pm_type) == 4);
static_assert(offsetof(playerState_s, bRunLeftGun) == 0x61);
static_assert(offsetof(playerState_s, weapon) == 0x144);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, heldWeapons) == 0x1d8);
static_assert(sizeof(PlayerHeldWeapon) == 0x1c);
static_assert(offsetof(PlayerHeldWeapon, weapon) == 0);
static_assert(offsetof(PlayerHeldWeapon, needsRechamber) == 0x15);
static_assert(offsetof(playerState_s, weapAnim) == 0x558);
static_assert(offsetof(playerState_s, weapAnimLeft) == 0x55c);

void rechamber_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[0] > 65535 || input[1] > 15 || input[2] > 0x7fff || input[6] > 1) std::abort();
    playerState_s ps{};
    ps.weapon = static_cast<unsigned short>(input[0]);
    std::memcpy(&ps.pm_type, input + 3, 4);
    std::memcpy(&ps.weapAnim, input + 4, 4);
    std::memcpy(&ps.weapAnimLeft, input + 5, 4);
    ps.bRunLeftGun = input[6] != 0;
    std::memcpy(&ps.weaponstate, input + 7, 4);
    for (unsigned i = 0; i < 15; ++i)
    {
        ps.heldWeapons[i].weapon = input[1] == i + 1 ? input[0] : 0x10000 + i;
        ps.heldWeapons[i].needsRechamber = (input[2] & (1u << i)) != 0;
    }
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
    PM_Weapon_FinishRechamber(&ps);
    std::memcpy(output, &ps.weaponstate, 4);
    std::memcpy(output + 1, &ps.weapAnim, 4);
    output[2] = 0;
    for (unsigned i = 0; i < 15; ++i)
    {
        output[2] |= unsigned(ps.heldWeapons[i].needsRechamber) << i;
        before.heldWeapons[i].needsRechamber = ps.heldWeapons[i].needsRechamber;
    }
    before.weaponstate = ps.weaponstate;
    before.weapAnim = ps.weapAnim;
    if (std::memcmp(&before, &ps, sizeof(ps))) std::abort();
}

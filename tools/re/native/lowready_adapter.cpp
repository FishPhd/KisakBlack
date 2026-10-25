#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_view.h>
#include <cgame/cg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>

static_assert(sizeof(playerState_s) == 0x26e8);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(sizeof(weaponState_t) == 0x90);
static_assert(offsetof(weaponState_t, ps) == 0);
static_assert(sizeof(WeaponDef) == 0x808);
static_assert(offsetof(WeaponDef, lowReadyInTime) == 0x424);
static_assert(offsetof(WeaponDef, lowReadyOutTime) == 0x42c);
static_assert(offsetof(WeaponDef, dtpInTime) == 0x43c);
static_assert(offsetof(WeaponDef, dtpOutTime) == 0x444);

// Raw signed state/time/in-duration/out-duration. Output is the exact float
// consumed by the original caller; the reconstructed double ABI is projected
// explicitly rather than treating its ABI as an original-code claim.
static void interpolation_call(bool dtp, bool original, void* entry, const unsigned* input, unsigned* output)
{
    for (unsigned i = 4; i < 16; ++i)
        if (input[i]) std::abort();
    playerState_s ps{};
    WeaponDef definition{};
    weaponState_t movement{};
    movement.ps = &ps;
    std::memcpy(&ps.weaponstate, input, 4);
    std::memcpy(&ps.weaponTime, input + 1, 4);
    std::memcpy(dtp ? &definition.dtpInTime : &definition.lowReadyInTime, input + 2, 4);
    std::memcpy(dtp ? &definition.dtpOutTime : &definition.lowReadyOutTime, input + 3, 4);
    const playerState_s before_ps = ps;
    const WeaponDef before_definition = definition;
    const weaponState_t before_movement = movement;
    float result;
#ifdef _WIN32
    if (original)
    {
        auto movement_pointer = &movement;
        auto definition_pointer = &definition;
        __asm {
            mov eax, definition_pointer
            push movement_pointer
            call entry
            add esp, 4
            movss result, xmm0
        }
    }
    else
#endif
        result = static_cast<float>(dtp
            ? BG_CalculateWeaponMovement_GetDTPLerp(&movement, &definition)
            : BG_CalculateWeaponMovement_GetLowReadyLerp(&movement, &definition));
    if (std::memcmp(&before_ps, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_movement, &movement, sizeof(movement))) std::abort();
    std::memcpy(output, &result, 4);
}

void lowready_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    interpolation_call(false, original, entry, input, output);
}

void dtp_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    interpolation_call(true, original, entry, input, output);
}

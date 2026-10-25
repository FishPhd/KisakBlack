#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_view.h>
#include <bgame/bg_perks.h>
#include <cgame/cg_weapons.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

// Shared with the complete pose caller's private fixture. This is a binding to
// a real setting structure, not a substituted calculation or constant result.
const dvar_t* perk_sprintRecoveryMultiplier = nullptr;

static_assert(sizeof(playerState_s) == 0x26e8);
static_assert(offsetof(playerState_s, weaponstate) == 0x158);
static_assert(offsetof(playerState_s, weaponTime) == 0x3c);
static_assert(offsetof(playerState_s, perks) + 4 == 0x530);
static_assert(sizeof(weaponState_t) == 0x90);
static_assert(offsetof(weaponState_t, ps) == 0);
static_assert(sizeof(WeaponDef) == 0x808);
static_assert(offsetof(WeaponDef, sprintInTime) == 0x418);
static_assert(offsetof(WeaponDef, sprintOutTime) == 0x420);
static_assert(offsetof(dvar_s, current) == 0x18);
static_assert(sizeof(dvar_s) == 0x70);

// Raw signed state/time/in-duration/out-duration, perks[1], multiplier bits.
// The result is projected from the reconstructed double ABI to the float bits
// consumed by the original caller; compiled ABI identity is outside this unit.
void sprint_lerp_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    for (unsigned i = 6; i < 16; ++i)
        if (input[i]) std::abort();
    float multiplier;
    std::memcpy(&multiplier, input + 5, 4);
    if (!std::isfinite(multiplier) || multiplier < -4.0f || multiplier > 4.0f) std::abort();
    playerState_s ps{};
    WeaponDef definition{};
    weaponState_t movement{};
    dvar_s setting{};
    movement.ps = &ps;
    std::memcpy(&ps.weaponstate, input, 4);
    std::memcpy(&ps.weaponTime, input + 1, 4);
    std::memcpy(&definition.sprintInTime, input + 2, 4);
    std::memcpy(&definition.sprintOutTime, input + 3, 4);
    std::memcpy(ps.perks + 1, input + 4, 4);
    std::memcpy(&setting.current.value, input + 5, 4);
    if (perk_sprintRecoveryMultiplier) std::abort();
    perk_sprintRecoveryMultiplier = &setting;
    const playerState_s before_ps = ps;
    const WeaponDef before_definition = definition;
    const weaponState_t before_movement = movement;
    const dvar_s before_setting = setting;
    float result;
#ifdef _WIN32
    if (original)
    {
        auto global = static_cast<unsigned char*>(entry) - 0x3c3fd0 + 0x863c28;
        DWORD protection, ignored;
        if (!VirtualProtect(global, 4, PAGE_READWRITE, &protection)) std::abort();
        std::memcpy(global, &perk_sprintRecoveryMultiplier, 4);
        if (!VirtualProtect(global, 4, protection, &ignored)) std::abort();
        auto movement_pointer = &movement;
        auto definition_pointer = &definition;
        __asm {
            mov eax, definition_pointer
            push movement_pointer
            call entry
            add esp, 4
            movss result, xmm0
        }
        if (std::memcmp(global, &perk_sprintRecoveryMultiplier, 4)) std::abort();
    }
    else
#endif
        result = static_cast<float>(BG_CalculateWeaponMovement_GetSprintLerp(&movement, &definition));
    if (std::memcmp(&before_ps, &ps, sizeof(ps)) ||
        std::memcmp(&before_definition, &definition, sizeof(definition)) ||
        std::memcmp(&before_movement, &movement, sizeof(movement)) ||
        std::memcmp(&before_setting, &setting, sizeof(setting)) ||
        perk_sprintRecoveryMultiplier != &setting) std::abort();
    perk_sprintRecoveryMultiplier = nullptr;
    std::memcpy(output, &result, 4);
}

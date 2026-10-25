#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_view.h>
#include <cgame/cg_weapons.h>
#include <xanim/xanim.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#endif

extern WeaponVariantDef* bg_weaponVariantDefs[2048];
extern const dvar_t* perk_sprintRecoveryMultiplier;
// Real production dependencies are selected, but the nonmantling domain must
// exit through the real Mantle_IsWeaponEquipped gate before these are touched.
const dvar_t* mantle_enable = nullptr;
const dvar_t* perk_mantleReduction = nullptr;
XAnim_s* s_mantleAnims = nullptr;

static_assert(sizeof(playerState_s) == 0x26e8);
static_assert(sizeof(weaponState_t) == 0x90);
static_assert(offsetof(weaponState_t, ps) == 0);
static_assert(sizeof(WeaponDef) == 0x808);
static_assert(offsetof(WeaponDef, vSprintOfs) == 0x20c);
static_assert(offsetof(WeaponDef, vLowReadyOfs) == 0x230);
static_assert(offsetof(WeaponDef, vDtpOfs) == 0x248);
static_assert(offsetof(WeaponDef, vMantleOfs) == 0x26c);
static_assert(offsetof(WeaponDef, vSlideOfs) == 0x284);
static_assert(offsetof(WeaponDef, sprintInTime) == 0x418);
static_assert(offsetof(WeaponDef, sprintOutTime) == 0x420);
static_assert(offsetof(WeaponDef, lowReadyInTime) == 0x424);
static_assert(offsetof(WeaponDef, lowReadyOutTime) == 0x42c);
static_assert(offsetof(WeaponDef, dtpInTime) == 0x43c);
static_assert(offsetof(WeaponDef, dtpOutTime) == 0x444);
static_assert(offsetof(WeaponDef, slideInTime) == 0x448);
static_assert(offsetof(playerState_s, perks) + 4 == 0x530);
static_assert(offsetof(playerState_s, mantleState) + offsetof(MantleState, timer) == 0x4fc);
static_assert(offsetof(playerState_s, mantleState) + offsetof(MantleState, flags) == 0x504);
static_assert(offsetof(dvar_s, current) == 0x18);

static unsigned coefficient(unsigned seed, unsigned asset, unsigned category, unsigned component)
{
    const unsigned mixed = seed ^ (0x9e3779b9u * (asset + 1)) ^
        (0x45d9f3bu * (category + 1)) ^ (0x1020304u * (component + 1));
    return (mixed & 0x80000000u) | 0x3f000000u | (mixed & 0x007fffffu);
}

struct PoseTarget { unsigned before; float value[3]; unsigned after; };

void weapon_pose_call(bool original, void* entry, const unsigned* input, unsigned* output)
{
    if (input[7] > 3 || (input[9] & 4) || input[14] || input[15] > 1 ||
        (input[5] & 0x7f800000) == 0x7f800000 ||
        mantle_enable || perk_mantleReduction || s_mantleAnims || perk_sprintRecoveryMultiplier)
        std::abort();
    float multiplier; std::memcpy(&multiplier, input + 5, 4);
    if (multiplier < -4.0f || multiplier > 4.0f) std::abort();
    playerState_s ps{};
    std::memcpy(&ps.weaponstate, input, 4);
    std::memcpy(&ps.weaponTime, input + 1, 4);
    std::memcpy(ps.perks + 1, input + 4, 4);
    std::memcpy(&ps.weapFlags, input + 6, 4);
    std::memcpy(&ps.pm_flags, input + 9, 4);
    std::memcpy(&ps.mantleState.flags, input + 10, 4);
    std::memcpy(&ps.mantleState.timer, input + 13, 4);
    ps.weapon = 1; ps.offHandIndex = 2; ps.meleeWeapon = 3;
    weaponState_t movement{}; movement.ps = &ps;
    WeaponDef definitions[3]{};
    WeaponVariantDef variants[3]{};
    const unsigned vector_offsets[] = {0x20c, 0x230, 0x248, 0x26c, 0x284};
    for (unsigned asset = 0; asset < 3; ++asset)
    {
        definitions[asset].bHasBayonet = (input[7] & 1) != 0;
        definitions[asset].bUseAsMelee = (input[7] & 2) != 0;
        for (unsigned category = 0; category < 5; ++category)
            for (unsigned i = 0; i < 6; ++i)
            {
                const unsigned word = coefficient(input[8], asset, category, i);
                std::memcpy(reinterpret_cast<unsigned char*>(definitions + asset) + vector_offsets[category] + i * 4, &word, 4);
            }
        for (const unsigned offset : {0x418u, 0x424u, 0x43cu, 0x448u})
            std::memcpy(reinterpret_cast<unsigned char*>(definitions + asset) + offset, input + 2, 4);
        for (const unsigned offset : {0x420u, 0x42cu, 0x444u})
            std::memcpy(reinterpret_cast<unsigned char*>(definitions + asset) + offset, input + 3, 4);
        variants[asset].weapDef = definitions + asset;
        bg_weaponVariantDefs[asset + 1] = variants + asset;
    }
    dvar_s recovery{}; std::memcpy(&recovery.current.value, input + 5, 4);
    perk_sprintRecoveryMultiplier = &recovery;
    PoseTarget pos{0x12345678, {}, 0x87654321}, angles{0x12345678, {}, 0x87654321};
    for (unsigned i = 0; i < 3; ++i)
    {
        const unsigned p = ((input[11] ^ (0x1234567u * (i + 1))) & 0x807fffffu) | 0x3f800000u;
        const unsigned a = ((input[12] ^ (0x1234567u * (i + 1))) & 0x807fffffu) | 0x3f800000u;
        std::memcpy(pos.value + i, &p, 4); std::memcpy(angles.value + i, &a, 4);
    }
    const playerState_s before_ps = ps;
    const weaponState_t before_movement = movement;
    WeaponDef before_definitions[3]; std::memcpy(before_definitions, definitions, sizeof(definitions));
    WeaponVariantDef before_variants[3]; std::memcpy(before_variants, variants, sizeof(variants));
    const dvar_s before_recovery = recovery;
#ifdef _WIN32
    if (original)
    {
        auto image = static_cast<unsigned char*>(entry) - 0x3c4130;
        auto table = image + 0x880258;
        auto setting_pointer = image + 0x863c28;
        DWORD old, ignored;
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), PAGE_READWRITE, &old)) std::abort();
        std::memcpy(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs));
        if (!VirtualProtect(table, sizeof(bg_weaponVariantDefs), old, &ignored)) std::abort();
        if (!VirtualProtect(setting_pointer, 4, PAGE_READWRITE, &old)) std::abort();
        *reinterpret_cast<const dvar_s**>(setting_pointer) = &recovery;
        if (!VirtualProtect(setting_pointer, 4, old, &ignored)) std::abort();
        reinterpret_cast<void(__cdecl*)(const weaponState_t*, float*, float*)>(entry)(&movement, pos.value, angles.value);
        if (std::memcmp(table, bg_weaponVariantDefs, sizeof(bg_weaponVariantDefs)) ||
            *reinterpret_cast<const dvar_s**>(setting_pointer) != &recovery) std::abort();
    }
    else
#endif
        BG_CalculateWeaponMovement_Base_Internal_WeaponState(&movement, pos.value, angles.value);
    if (std::memcmp(&before_ps, &ps, sizeof(ps)) || std::memcmp(&before_movement, &movement, sizeof(movement)) ||
        std::memcmp(before_definitions, definitions, sizeof(definitions)) || std::memcmp(before_variants, variants, sizeof(variants)) ||
        std::memcmp(&before_recovery, &recovery, sizeof(recovery)) || perk_sprintRecoveryMultiplier != &recovery ||
        mantle_enable || perk_mantleReduction || s_mantleAnims || pos.before != 0x12345678 || pos.after != 0x87654321 ||
        angles.before != 0x12345678 || angles.after != 0x87654321) std::abort();
    if (input[15]) { std::memcpy(output, angles.value, 12); std::memcpy(output + 3, pos.value, 4); }
    else { std::memcpy(output, pos.value, 12); std::memcpy(output + 3, angles.value, 4); }
    for (unsigned i = 0; i < 2048; ++i)
        if (bg_weaponVariantDefs[i] != (i >= 1 && i <= 3 ? variants + i - 1 : nullptr)) std::abort();
    for (unsigned i = 1; i <= 3; ++i) bg_weaponVariantDefs[i] = nullptr;
    perk_sprintRecoveryMultiplier = nullptr;
}

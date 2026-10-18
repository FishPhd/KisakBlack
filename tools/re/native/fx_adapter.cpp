#include <EffectsCore/fx_draw.h>
#include <EffectsCore/fx_update_util.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#define KISAK_RE_CODEGEN_LAYOUT_ONLY
namespace codegen_abi {
#include "codegen_abi.h"
}
#undef KISAK_RE_CODEGEN_LAYOUT_ONLY

#define CHECK_CODEGEN_LAYOUT(type, field) static_assert(offsetof(type, field) == offsetof(codegen_abi::type, field))
static_assert(sizeof(FxElemDef) == sizeof(codegen_abi::FxElemDef));
static_assert(sizeof(FxElemPreVisualState) == sizeof(codegen_abi::FxElemPreVisualState));
static_assert(sizeof(FxElemVelStateSample) == sizeof(codegen_abi::FxElemVelStateSample));
static_assert(sizeof(FxElemVisStateSample) == sizeof(codegen_abi::FxElemVisStateSample));
static_assert(sizeof(orientation_t) == sizeof(codegen_abi::orientation_t));
CHECK_CODEGEN_LAYOUT(FxElemDef, flags);
CHECK_CODEGEN_LAYOUT(FxElemDef, velIntervalCount);
CHECK_CODEGEN_LAYOUT(FxElemDef, visStateIntervalCount);
CHECK_CODEGEN_LAYOUT(FxElemDef, velSamples);
CHECK_CODEGEN_LAYOUT(FxElemDef, visSamples);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, sampleLerp);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, sampleLerpInv);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, elemDef);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, effect);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, refState);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, randomSeed);
CHECK_CODEGEN_LAYOUT(FxElemPreVisualState, distanceFade);
CHECK_CODEGEN_LAYOUT(FxElemVelStateSample, local);
CHECK_CODEGEN_LAYOUT(FxElemVelStateSample, world);
CHECK_CODEGEN_LAYOUT(FxElemVelStateInFrame, velocity);
CHECK_CODEGEN_LAYOUT(FxElemVec3Range, base);
CHECK_CODEGEN_LAYOUT(FxElemVec3Range, amplitude);
CHECK_CODEGEN_LAYOUT(orientation_t, axis);
#undef CHECK_CODEGEN_LAYOUT

static_assert(sizeof(FxElemDef) == 0x124);
static_assert(offsetof(FxElemDef, visStateIntervalCount) == 0xbb);
static_assert(offsetof(FxElemDef, visSamples) == 0xc0);
static_assert(sizeof(FxElemVisStateSample) == 0x30);
static_assert(sizeof(FxElemPreVisualState) == 0x1c);
static_assert(offsetof(FxElemPreVisualState, refState) == 0x10);
static_assert(offsetof(FxElemPreVisualState, distanceFade) == 0x18);

void fx_call(bool original, void* entry, const unsigned* input, unsigned* output) {
    float time; std::memcpy(&time, input + 1, 4);
    // Pointer arithmetic remains inside allocated samples. No FX evaluator or
    // sample dereference is covered by this setup-only adapter.
    if (input[0] > 255 || !(time >= 0.f && time <= 1.125f)) std::abort();
    FxElemVisStateSample samples[512]{};
    FxElemDef def{};
    def.visStateIntervalCount = static_cast<unsigned char>(input[0]);
    def.visSamples = samples;
    const auto before_def = def;
    const auto effect = reinterpret_cast<const FxEffect*>(&def);
    struct Guarded { unsigned left[4]; FxElemPreVisualState state; unsigned right[4]; } guard{};
    for (unsigned i = 0; i != 4; ++i) guard.left[i] = guard.right[i] = 0xa55a55aa;
    using Setup = void (__cdecl *)(const FxElemDef*, const FxEffect*, int, float, FxElemPreVisualState*);
    auto setup = original ? reinterpret_cast<Setup>(entry) : FX_SetupVisualState;
    setup(&def, effect, static_cast<int>(input[2]), time, &guard.state);
    if (guard.state.elemDef != &def || guard.state.effect != effect ||
        static_cast<unsigned>(guard.state.randomSeed) != input[2] ||
        std::memcmp(&def, &before_def, sizeof(def))) std::abort();
    for (unsigned i = 0; i != 4; ++i)
        if (guard.left[i] != 0xa55a55aa || guard.right[i] != 0xa55a55aa) std::abort();
    std::memcpy(output, &guard.state.sampleLerp, 8);
    output[2] = static_cast<unsigned>(guard.state.refState - samples);
    output[3] = guard.state.distanceFade;
}

void fx_velocity_call(bool original, void* entry, const unsigned* input, unsigned* output) {
    float life, elapsed;
    std::memcpy(&life, input + 2, 4); std::memcpy(&elapsed, input + 3, 4);
    if (input[0] < 1 || input[0] > 255 || input[4] > 255 || input[9] > 1 ||
        (input[1] & ~0x3000000u) || !(life > 0.f && life <= 100000.f) ||
        !(elapsed >= 0.f && elapsed <= life)) std::abort();
    // The extra endpoint sample makes setup-at-endpoint reads defined in the
    // harness. This does not establish that real game assets contain it.
    FxElemVelStateSample samples[257]{};
    for (unsigned i = 0; i != 257; ++i) {
        auto values = reinterpret_cast<float*>(&samples[i]);
        for (unsigned j = 0; j != 24; ++j)
            values[j] = static_cast<float>(static_cast<int>((i * 13u + j * 7u + input[5]) % 255u) - 127) * 0.0625f;
    }
    static_assert(sizeof(FxElemVelStateSample) == 0x60);
    static_assert(offsetof(FxElemDef, velSamples) == 0xbc);
    FxElemDef def{}; def.velIntervalCount = static_cast<unsigned char>(input[0]);
    def.flags = input[1]; def.velSamples = samples;
    orientation_t orient{};
    for (unsigned i = 0; i != 3; ++i) orient.axis[i][i] = 1.f;
    if (input[9]) {
        orient.axis[0][0] = .6f; orient.axis[0][1] = .8f;
        orient.axis[1][0] = -.8f; orient.axis[1][1] = .6f;
    }
    float base[3]; std::memcpy(base, input + 6, 12);
    const auto before_def = def; const auto before_orient = orient;
    struct Guarded { unsigned left[4]; float velocity[3]; unsigned right[4]; } guard{};
    for (unsigned i = 0; i != 4; ++i) guard.left[i] = guard.right[i] = 0xa55a55aa;
    using Velocity = void (__cdecl *)(const FxElemDef*, int, float, float, const orientation_t*, const float*, float*);
    auto velocity = original ? reinterpret_cast<Velocity>(entry) : FX_GetVelocityAtTime;
    velocity(&def, input[4], life, elapsed, &orient, base, guard.velocity);
    if (std::memcmp(&def, &before_def, sizeof(def)) || std::memcmp(&orient, &before_orient, sizeof(orient)) ||
        std::memcmp(base, input + 6, 12)) std::abort();
    for (unsigned i = 0; i != 4; ++i)
        if (guard.left[i] != 0xa55a55aa || guard.right[i] != 0xa55a55aa) std::abort();
    std::memcpy(output, guard.velocity, 12);
    for (unsigned i = 0; i != 257; ++i) {
        const auto values = reinterpret_cast<const float*>(&samples[i]);
        for (unsigned j = 0; j != 24; ++j) {
            const float expected = static_cast<float>(static_cast<int>((i * 13u + j * 7u + input[5]) % 255u) - 127) * 0.0625f;
            if (std::memcmp(values + j, &expected, 4)) std::abort();
        }
    }
}

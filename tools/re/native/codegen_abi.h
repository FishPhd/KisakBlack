// VC9 declarations for isolated code generation. No implementation lives here.
// Full production layouts are checked independently by fx_adapter.cpp compiled
// against the real headers; these bindings expose only fields read by setup.
struct FxEffect;
struct FxElemVisStateSample { unsigned char storage[0x30]; };
struct FxElemVec3Range { float base[3]; float amplitude[3]; };
struct FxElemVelStateInFrame { FxElemVec3Range velocity; FxElemVec3Range totalDelta; };
struct FxElemVelStateSample { FxElemVelStateInFrame local; FxElemVelStateInFrame world; };
struct FxElemDef {
    int flags;
    unsigned char prefix[0xba - 4];
    unsigned char velIntervalCount;
    unsigned char visStateIntervalCount;
    const FxElemVelStateSample* velSamples;
    const FxElemVisStateSample* visSamples;
    unsigned char suffix[0x124 - 0xc4];
};
struct FxElemPreVisualState {
    float sampleLerp;
    float sampleLerpInv;
    const FxElemDef* elemDef;
    const FxEffect* effect;
    const FxElemVisStateSample* refState;
    int randomSeed;
    unsigned int distanceFade;
};
struct orientation_t { float origin[3]; float axis[3][3]; };
#ifndef KISAK_RE_CODEGEN_LAYOUT_ONLY
extern const float fx_randomTable[];
void __cdecl FX_GetVelocityAtTimeInFrame(const FxElemVelStateInFrame*, const FxElemVelStateInFrame*, const float*, const float*, float*);
void __cdecl FX_OrientationDirToWorldDir(const orientation_t*, const float*, float*);
#define iassert(...)
#define vassert(...)
extern "C" double __cdecl floor(double);
#endif

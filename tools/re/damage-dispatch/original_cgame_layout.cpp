// Original instruction-backed boundaries. A passing prefix must never be
// reported as whole-cg_s or rendering equivalence.
#include <cgame_mp/cg_local_mp.h>
#include <cstddef>
static_assert(alignof(snapshot_s) == 128);
static_assert(sizeof(snapshot_s) == 0x20300, "Original snapshot selection at 0x7f2ab2");
static_assert(offsetof(cg_s, activeSnapshots) == 0x80);
static_assert(offsetof(cg_s, frameInterpolation) == 0x40680);
static_assert(offsetof(cg_s, time) == 0x40688);
static_assert(offsetof(cg_s, predictedPlayerState) == 0x4069c);
static_assert(offsetof(cg_s, refdef) == 0x43100);
static_assert(offsetof(refdef_s, vieworg) == 0x20);
static_assert(offsetof(refdef_s, viewaxis) == 0x34);
static_assert(offsetof(refdef_s, time) == 0x58);
static_assert(offsetof(refdef_s, primaryLights) == 0x1500);
static_assert(sizeof(GfxLight) == 0x170);
static_assert(offsetof(GfxLight, viewMatrix) == 0xe0);
static_assert(offsetof(GfxLight, projMatrix) == 0x120);
static_assert(offsetof(GfxLight, def) == 0x160);
static_assert(sizeof(refdef_s) == 0x18520);
static_assert(offsetof(cg_s, refdefViewAngles) == 0x5b620);
static_assert(offsetof(cg_s, bgs) + offsetof(bgs_t, clientinfo) == 0x5f238,
              "Original live client-info base at 0x7ceae7");
static_assert(sizeof(visionSetVars_t) == 0x2a0, "Original cache stride at 0x7f8d3a");
static_assert(offsetof(cg_s, visionSetPreLoaded) == 0x6b2ec);
static_assert(offsetof(cg_s, visionSetPreLoadedName) == 0x6c7ec);
static_assert(offsetof(cg_s, visionSetCurrent) == 0x6e96c);
static_assert(offsetof(cg_s, visionNameNaked) == 0x6f974);
static_assert(offsetof(cg_s, resetWeaponStateFlag) == 0x718e8);
static_assert(offsetof(cg_s, serverVolFog) == 0x718ec);
static_assert(sizeof(serverVolFog_t) == 17 * sizeof(float));
static_assert(offsetof(serverVolFog_t, color) == 0x4);
static_assert(offsetof(serverVolFog_t, density) == 0x10);
static_assert(offsetof(serverVolFog_t, heightDensity) == 0x14);
static_assert(offsetof(serverVolFog_t, baseHeight) == 0x18);
static_assert(offsetof(serverVolFog_t, fogColorScale) == 0x1c);
static_assert(offsetof(serverVolFog_t, sunFogColor) == 0x20);
static_assert(offsetof(serverVolFog_t, sunFogDir) == 0x2c);
static_assert(offsetof(serverVolFog_t, sunFogStartAng) == 0x38);
static_assert(offsetof(serverVolFog_t, sunFogEndAng) == 0x3c);
static_assert(offsetof(cg_s, serverVolFog) + offsetof(serverVolFog_t, maxFogOpacity) == 0x7192c);

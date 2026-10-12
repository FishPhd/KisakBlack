// Compile-time layout checks for everything the fastfile loader maps straight onto file bytes.
//
// The values are what the original MSVC x86 build produces (the file format is defined by that
// layout). They are asserted on every platform so a different compiler/ABI (clang for the
// Emscripten web build: bitfield packing, enum or wchar_t width, 8-byte alignment of int64) fails
// the build here instead of silently corrupting asset loading.

#include <database/db_assetnames.h>
#include <database/db_registry.h>
#include <database/database.h>
#include <gfx_d3d/r_material.h>
#include <xanim/xmodel.h>
#include <universal/q_shared.h>
#include <physics/physpreset_load_obj.h>
#include <physics/physconstraints_load_obj.h>
#include <glass/glass.h>
#include <database/db_load.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_local.h>
#include <gfx_d3d/r_bsp_load_obj.h>
#include <bgame/bg_emblems.h>
#include <qcommon/com_bsp.h>
#include <gfx_d3d/r_extracam.h>
#include <gfx_d3d/r_gfx.h>
#include <gfx_d3d/r_image.h>
#include <EffectsCore/fx_system.h>
#include <sound/snd_bank.h>
#include <ui/ui_shared.h>
#include <xanim/xanim.h>
#include <universal/com_stringtable.h>

#define KISAK_ASSERT_SIZE(T, N) static_assert(sizeof(T) == (N), "sizeof(" #T ") must match the MSVC x86 layout (" #N ")")

// Fastfile pointers are 32-bit offsets/addresses: the format only works with 4-byte pointers.
static_assert(sizeof(void *) == 4, "fastfile structs assume 32-bit pointers");
static_assert(sizeof(int) == 4 && sizeof(short) == 2 && sizeof(long long) == 8, "base type widths");

// ----- XAssetHeader members -----
KISAK_ASSERT_SIZE(XModelPieces, 12);
KISAK_ASSERT_SIZE(PhysPreset, 84);
KISAK_ASSERT_SIZE(PhysConstraints, 2696);
KISAK_ASSERT_SIZE(DestructibleDef, 24);
KISAK_ASSERT_SIZE(XAnimParts, 104);
KISAK_ASSERT_SIZE(XModel, 252);
KISAK_ASSERT_SIZE(Material, 192);
KISAK_ASSERT_SIZE(MaterialPixelShader, 16);
KISAK_ASSERT_SIZE(MaterialVertexShader, 16);
KISAK_ASSERT_SIZE(MaterialTechniqueSet, 528);
KISAK_ASSERT_SIZE(GfxImage, 52);
KISAK_ASSERT_SIZE(SndBank, 40);
KISAK_ASSERT_SIZE(SndPatch, 20);
KISAK_ASSERT_SIZE(clipMap_t, 332);
KISAK_ASSERT_SIZE(ComWorld, 64);
KISAK_ASSERT_SIZE(GfxWorld, 1084);
KISAK_ASSERT_SIZE(GfxLightDef, 16);
KISAK_ASSERT_SIZE(Font_s, 24);
KISAK_ASSERT_SIZE(MenuList, 12);
KISAK_ASSERT_SIZE(menuDef_t, 400);
KISAK_ASSERT_SIZE(WeaponVariantDef, 228);
KISAK_ASSERT_SIZE(SndDriverGlobals, 52);
KISAK_ASSERT_SIZE(FxEffectDef, 60);
KISAK_ASSERT_SIZE(RawFile, 12);
KISAK_ASSERT_SIZE(StringTable, 20);
KISAK_ASSERT_SIZE(PackIndex, 28);
KISAK_ASSERT_SIZE(XGlobals, 40);
KISAK_ASSERT_SIZE(ddlRoot_t, 8);
KISAK_ASSERT_SIZE(Glasses, 56);
KISAK_ASSERT_SIZE(EmblemSet, 44);
KISAK_ASSERT_SIZE(XAsset, 8);

// ----- Structures reached through asset pointers (bitfield / enum / alignment sensitive) -----
KISAK_ASSERT_SIZE(XSurface, 68);
KISAK_ASSERT_SIZE(XSurfaceVertexInfo, 16);
KISAK_ASSERT_SIZE(XModelLodInfo, 32);
KISAK_ASSERT_SIZE(XBoneInfo, 44);
KISAK_ASSERT_SIZE(DObjAnimMat, 32);
KISAK_ASSERT_SIZE(XModelCollSurf_s, 44);
KISAK_ASSERT_SIZE(XRigidVertList, 12);
KISAK_ASSERT_SIZE(XSurfaceCollisionTree, 40);
KISAK_ASSERT_SIZE(XModelStreamInfo, 4);
KISAK_ASSERT_SIZE(MaterialTechnique, 28);
KISAK_ASSERT_SIZE(MaterialPass, 20);
KISAK_ASSERT_SIZE(MaterialTextureDef, 16);
KISAK_ASSERT_SIZE(MaterialConstantDef, 32);
KISAK_ASSERT_SIZE(MaterialShaderArgument, 8);
KISAK_ASSERT_SIZE(MaterialVertexDeclaration, 108);
KISAK_ASSERT_SIZE(MaterialStateMap, 44);
KISAK_ASSERT_SIZE(GfxStateBits, 8);
KISAK_ASSERT_SIZE(GfxImageLoadDef, 16);
KISAK_ASSERT_SIZE(GfxTexture, 4);
KISAK_ASSERT_SIZE(GfxPixelShaderLoadDef, 8);
KISAK_ASSERT_SIZE(GfxVertexShaderLoadDef, 8);
KISAK_ASSERT_SIZE(GfxWorldDpvsStatic, 112);
KISAK_ASSERT_SIZE(GfxWorldDpvsDynamic, 48);
KISAK_ASSERT_SIZE(GfxSurface, 80);
KISAK_ASSERT_SIZE(GfxStaticModelDrawInst, 76);
KISAK_ASSERT_SIZE(GfxLightGrid, 56);
KISAK_ASSERT_SIZE(GfxCell, 56);
KISAK_ASSERT_SIZE(GfxPortal, 68);
KISAK_ASSERT_SIZE(GfxAabbTree, 40);
KISAK_ASSERT_SIZE(GfxBrushModel, 60);
KISAK_ASSERT_SIZE(GfxLightRegion, 8);
KISAK_ASSERT_SIZE(GfxCullGroup, 32);
KISAK_ASSERT_SIZE(GfxWorldVertexData, 8);
KISAK_ASSERT_SIZE(GfxWorldVertexLayerData, 8);
KISAK_ASSERT_SIZE(GfxSkyDynamicIntensity, 16);
KISAK_ASSERT_SIZE(SunLightParseParams, 180);
KISAK_ASSERT_SIZE(cbrush_t, 96);
KISAK_ASSERT_SIZE(cLeaf_s, 44);
KISAK_ASSERT_SIZE(cplane_s, 20);
KISAK_ASSERT_SIZE(CollisionPartition, 20);
KISAK_ASSERT_SIZE(CollisionAabbTree, 32);
KISAK_ASSERT_SIZE(cmodel_t, 72);
KISAK_ASSERT_SIZE(cbrushside_t, 12);
KISAK_ASSERT_SIZE(dmaterial_t, 72);
KISAK_ASSERT_SIZE(FxElemDef, 292);
KISAK_ASSERT_SIZE(FxElemVisuals, 4);

#undef KISAK_ASSERT_SIZE

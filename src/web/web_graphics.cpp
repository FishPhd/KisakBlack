#include "web_graphics.h"
#include <universal/dvar.h>
#include <web/gpu/web_gpu.h>
#include "web_preparation_policy.h"
#include <gfx_d3d/r_init.h>
#include <gfx_d3d/r_dvars.h>
#include <atomic>

const dvar_s *web_disableDistortion;
const dvar_s *web_disableMotionBlur;
const dvar_s *web_reduceDynamicLights;
const dvar_s *web_warmShaders;
const dvar_s *r_shaderWarming;
const dvar_s *web_disableWeaponFiringEffects;
static const dvar_s *web_prepareStaticImages;
static std::atomic<int> managedPreparation{2};
void Web_SetPreparationMode(int mode) { managedPreparation.store(mode); }
bool Web_ShaderWarmingEnabled()
{
    // preparation=off is an explicit launch skip. Any other mode follows the menu toggle.
    if (managedPreparation.load() == 0) return false;
    return r_shaderWarming && r_shaderWarming->current.enabled;
}

void Web_UpdateGraphicsSettings()
{
    // The UI owns AnisoMin. Upgrade a legacy hidden 1x ceiling when the user
    // moves that slider, and reflect hardware clamps back into its value.
    // Adapter caps are queried before the GL context exists. Use the actual
    // device's extension limit here, including WebGL without anisotropy support.
    const int limit = Web_MaxAnisotropy();
    if (r_texFilterAnisoMax->current.integer > limit)
        Dvar_SetInt((dvar_s *)r_texFilterAnisoMax, limit);
    if (r_texFilterAnisoMin->current.integer > limit)
        Dvar_SetInt((dvar_s *)r_texFilterAnisoMin, limit);
    if (r_texFilterAnisoMax->current.integer < r_texFilterAnisoMin->current.integer)
        Dvar_SetInt((dvar_s *)r_texFilterAnisoMax, r_texFilterAnisoMin->current.integer);
}

void Web_BrowserFullscreenChanged(bool enabled)
{
    // Called on the game thread through the input queue. Browser exits and
    // rejected requests must update both the applied and staged menu values.
    Dvar_SetBool((dvar_s *)r_fullscreen, enabled);
    Dvar_SetBoolByName("ui_r_fullscreen", enabled);
    vidConfig.isFullscreen = enabled;
}

bool Web_UseFullTexturePreparation() {
    return Web_ResidentStaticImagePolicy(web_prepareStaticImages && web_prepareStaticImages->current.enabled,
        webgpu::Requested());
}

void Web_RegisterGraphicsDvars()
{
    web_disableWeaponFiringEffects = _Dvar_RegisterBool("web_disableWeaponFiringEffects", false, 1,
        "Disable weapon muzzle, casing, tracer and bullet impact visuals in the browser");
    web_disableDistortion = _Dvar_RegisterBool("web_disableDistortion", false, 1, "Disable distortion effects in the browser");
    web_disableMotionBlur = _Dvar_RegisterBool("web_disableMotionBlur", false, 1, "Skip motion blur passes in the browser");
    web_reduceDynamicLights = _Dvar_RegisterBool("web_reduceDynamicLights", false, 1, "Limit dynamic lights to one in the browser");
    r_shaderWarming = _Dvar_RegisterBool("r_shaderWarming", true, 1, "Cache shaders before they are first needed");
    web_warmShaders = _Dvar_RegisterBool("web_warmShaders", true, 0, "Legacy alias; r_shaderWarming controls shader caching");
    web_prepareStaticImages = _Dvar_RegisterBool("web_prepareStaticImages", false, 0, "Keep selected static image mips resident during full WebGPU preparation");
}

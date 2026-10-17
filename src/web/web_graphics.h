#pragma once

struct dvar_s;
extern const dvar_s *web_disableDistortion;
extern const dvar_s *web_disableMotionBlur;
extern const dvar_s *web_reduceDynamicLights;
extern const dvar_s *web_warmShaders;
void Web_SetPreparationMode(int mode);
void Web_PrepareLegacyMap(const char *map, int localClientNum);
extern const dvar_s *web_disableWeaponFiringEffects;
void Web_RegisterGraphicsDvars();
void Web_BrowserFullscreenChanged(bool enabled);
void Web_UpdateGraphicsSettings();
int Web_MaxAnisotropy();
bool Web_UseFullTexturePreparation();
struct UiContext;
struct menuDef_t;
void Web_EnsureGraphicsMenu(UiContext *dc, menuDef_t *menu);

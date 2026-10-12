#pragma once

struct dvar_s;
extern const dvar_s *web_disableDistortion;
extern const dvar_s *web_disableMotionBlur;
extern const dvar_s *web_reduceDynamicLights;
extern const dvar_s *web_warmShaders;
void Web_RegisterGraphicsDvars();
struct UiContext;
struct menuDef_t;
void Web_EnsureGraphicsMenu(UiContext *dc, menuDef_t *menu);

#include "web_graphics.h"
#include <universal/dvar.h>

const dvar_s *web_disableDistortion;
const dvar_s *web_disableMotionBlur;
const dvar_s *web_reduceDynamicLights;
const dvar_s *web_warmShaders;

void Web_RegisterGraphicsDvars()
{
    web_disableDistortion = _Dvar_RegisterBool("web_disableDistortion", false, 1, "Disable distortion effects in the browser");
    web_disableMotionBlur = _Dvar_RegisterBool("web_disableMotionBlur", false, 1, "Skip motion blur passes in the browser");
    web_reduceDynamicLights = _Dvar_RegisterBool("web_reduceDynamicLights", false, 1, "Limit dynamic lights to one in the browser");
    web_warmShaders = _Dvar_RegisterBool("web_warmShaders", false, 1, "Prepare loaded shaders before play; increases loading time");
}

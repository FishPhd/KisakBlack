#pragma once
#include "cscr_main.h"

// A compile-only diagnostic. The caller must terminate before any script runs.
void Scr_BeginBindingAudit(bool enabled, scriptInstance_t instance = SCRIPTINSTANCE_SERVER);
bool Scr_EndBindingAudit(unsigned int *unresolved);

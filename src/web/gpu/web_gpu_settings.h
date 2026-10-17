#pragma once
#include <universal/dvar.h>
#include <cstring>

namespace webgpu {
// Float polygon-offset settings use their exact bit identity (normalizing zero).
// A bool occupies one byte of DvarValue; its other bytes can retain a previous
// value and must not become part of a portable cache identity.
inline int PipelineSettingValue(const dvar_s *setting) {
    if (!setting) return 0;
    if (setting->type == DVAR_TYPE_FLOAT) {
        if (setting->current.value == 0.0f) return 0;
        int bits; std::memcpy(&bits, &setting->current.value, sizeof bits); return bits;
    }
    return setting->type == DVAR_TYPE_BOOL ? int(setting->current.enabled) : setting->current.integer;
}
}

#pragma once
#include <web/gl/web_sm3.h>
namespace webgpu {
// Pipeline layouts need reflection, not the GLSL backend's generated program.
// Keeping Result::glsl in every state variant multiplied shader text by tens
// of thousands of recipes and exhausted wasm32 across map transitions.
inline sm3::Result PipelineInterface(const sm3::Result &source) {
    sm3::Result result;
    result.floatCount = source.floatCount;
    result.samplers = source.samplers;
    return result;
}
}

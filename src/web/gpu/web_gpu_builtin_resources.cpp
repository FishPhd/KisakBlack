#include "web_gpu.h"
#include <gfx_d3d/r_buffers.h>
#include <gfx_d3d/r_material.h>

extern GfxImage g_imageProgs[29];

namespace webgpu {
void VisitBuiltinResources(void (*image)(GfxImage *),
    void (*vertex)(IDirect3DVertexBuffer9 *, const char *),
    void (*index)(IDirect3DIndexBuffer9 *, const char *)) {
    for (auto &programImage : g_imageProgs) image(&programImage);
    vertex(gfxBuf.smodelCacheVb, "static-model cache");
    vertex(gfxBuf.particleCloudVertexBuffer, "particle-cloud vertices");
    index(gfxBuf.particleCloudIndexBuffer, "particle-cloud indices");
}
}

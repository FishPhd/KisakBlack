#pragma once
#include <cstdint>
#include <d3d9.h>
struct GfxImage;
namespace kd {
class Device;
struct KSurface;
struct BufferCore;
struct TexCore;
}
namespace webgpu {
bool Requested();
bool Start(unsigned width, unsigned height);
void Resize(unsigned width, unsigned height);
void Draw(kd::Device &, D3DPRIMITIVETYPE, unsigned count, int base, unsigned first,
          const void *vertices = nullptr, unsigned vertexBytes = 0, unsigned stride = 0,
          const void *indices = nullptr, D3DFORMAT indexFormat = D3DFMT_INDEX16,
          unsigned minVertex = 0, unsigned vertexCount = 0);
void BeginMapPreparation();
void WarmMap(const char *map, int localClientNum = 0);
void WarmFrontend();
void WarmBuiltinResources();
// Engine-owned resources absent from the asset database; called while synced.
void VisitBuiltinResources(void (*image)(GfxImage *),
    void (*vertex)(IDirect3DVertexBuffer9 *, const char *),
    void (*index)(IDirect3DIndexBuffer9 *, const char *));
void CheckPreparation();
// Safe from DB workers; the game thread prepares a stable dependency snapshot.
void NotifyPreparationAsset(int type, const char *name);
void CancelWarmup();
bool Settling();
int TestDeviceLoss(); // Agent-only caller in web_main.cpp.
void Clear(kd::Device &, DWORD count, const D3DRECT *, DWORD flags, D3DCOLOR, float, DWORD);
void Fill(kd::KSurface *, const RECT *, D3DCOLOR);
void Copy(kd::KSurface *, const RECT *, kd::KSurface *, const RECT *, D3DTEXTUREFILTERTYPE);
void Present(kd::Device &);
void Release(uint64_t id);
}

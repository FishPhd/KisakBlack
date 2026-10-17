// Internals of the Direct3D 9 -> WebGL2 emulation layer (src/web/gl/web_d3d9_*.cpp).
//
// The engine talks to a D3D9 device from several threads (main, database streaming, render). WebGL2
// contexts live on exactly one thread, so the layer is split in two halves:
//   * CPU half  - resources, state and shadow copies; callable from any thread, never touches GL.
//   * GL half   - realization (lazy GL object creation / upload), state application, draws, present;
//                 runs only on the thread that created the device (the engine's render thread).
// Releasing a resource on any thread queues its GL objects for deletion on the GL thread.
#pragma once
#include <d3d9.h>

#include <GLES3/gl3.h>
#include <GLES3/gl2ext.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "web_sm3.h"

namespace kd
{
class Device;
struct KSurface;
struct KVertexBuffer;
struct KIndexBuffer;
struct KDecl;
struct KVShader;
struct KPShader;
struct TexCore;
TexCore *TexCoreOf(IDirect3DBaseTexture9 *t);

// ------------------------------------------------------------------------------------------------
// JS glue (web_gl_glue.cpp)
// ------------------------------------------------------------------------------------------------
bool GlInit(int w, int h);       // creates the OffscreenCanvas + WebGL2 context on the calling thread
void GlResize(int w, int h);
void GlPresent(unsigned draws);  // hands the canvas contents to the page
bool GlHasExt(const char *name);
int GlMaxTextureSize();
void GlLog(const char *text);    // console.log without going through the engine

// ------------------------------------------------------------------------------------------------
// Formats
// ------------------------------------------------------------------------------------------------
enum Swz : uint8_t
{
    SWZ_NONE = 0, SWZ_BGRA, SWZ_BGR1, SWZ_A8, SWZ_L8, SWZ_A8L8, SWZ_RGB1
};

enum Conv : uint8_t
{
    CV_NONE = 0, CV_R8G8B8, CV_A4R4G4B4, CV_A1R5G5B5, CV_X1R5G5B5, CV_X4R4G4B4
};

struct FmtInfo
{
    D3DFORMAT fmt;
    uint8_t cpuBytes;   // bytes per pixel (or per block) in D3D memory layout
    bool compressed;
    bool depth;
    bool stencil;
    GLenum internalFormat;
    GLenum format;
    GLenum type;
    Swz swz;
    Conv conv;
    bool colorRenderable;
};
const FmtInfo *FindFmt(D3DFORMAT f);
size_t LevelPitch(const FmtInfo &fi, UINT w);
size_t LevelRows(const FmtInfo &fi, UINT h);

constexpr D3DFORMAT FMT_INTZ = (D3DFORMAT)D3DMAKEFOURCC('I', 'N', 'T', 'Z');

// ------------------------------------------------------------------------------------------------
// COM plumbing
// ------------------------------------------------------------------------------------------------
template <class I> struct Unk : I
{
    std::atomic<int> rc{1};
    HRESULT QueryInterface(REFIID, void **pp) override
    {
        if (pp)
            *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return (ULONG)++rc; }
    ULONG Release() override
    {
        const int r = --rc;
        if (r == 0)
            OnZero();
        return (ULONG)r;
    }
    virtual void OnZero() { delete this; }
    virtual ~Unk() = default;
};

// Deferred GL object deletion (any thread -> GL thread).
enum GlKind : uint8_t { GLK_TEXTURE, GLK_RENDERBUFFER, GLK_BUFFER, GLK_PROGRAM, GLK_FRAMEBUFFER, GLK_SAMPLER };

// ------------------------------------------------------------------------------------------------
// Textures
// ------------------------------------------------------------------------------------------------
struct Level
{
    std::vector<uint8_t> data;
    bool dirty = false;
};

struct TexCore
{
    uint64_t gpuId = 0;
    Device *dev = nullptr;
    int type = 0; // 0 = 2D, 1 = cube, 2 = volume
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    const FmtInfo *fi = nullptr;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_MANAGED;
    UINT w = 0, h = 0, d = 1, levels = 1;
    std::vector<Level> lv; // [face * levels + level]
    std::mutex mu;
    GLuint tex = 0;
    bool anyDirty = false;
    bool mipDirty = false;
    DWORD lod = 0;             // SetLOD: most detailed mip the sampler may use
    uint32_t authored = 0;     // bit per mip whose faces have all been uploaded
    uint8_t faceGot[32] = {};  // bit per face uploaded at that mip
    bool mipsGenerated = false;
    int appliedBase = -1;      // GL_TEXTURE_BASE_LEVEL last sent
    int appliedMax = -1;
    int srgbMode = 0;          // 0 unset, 1 linear storage, 2 hardware sRGB, 3 mixed (shader fallback)
    GLenum storageInternal = 0;
    GLenum uploadType = 0;     // 0 = use fi->type
    bool uploadHalf = false;   // unorm16 stored as *16F because EXT_texture_norm16 is missing
    float appliedAniso = 0;
    D3DTEXTUREFILTERTYPE autoFilter = D3DTEXF_LINEAR;
    std::vector<KSurface *> surfaces; // cached level surfaces

    int faces() const { return type == 1 ? 6 : 1; }
    UINT levelW(UINT l) const { return (w >> l) ? (w >> l) : 1; }
    UINT levelH(UINT l) const { return (h >> l) ? (h >> l) : 1; }
    UINT levelD(UINT l) const { return (d >> l) ? (d >> l) : 1; }
    bool isRT() const { return (usage & D3DUSAGE_RENDERTARGET) != 0; }
    bool isDS() const { return (usage & D3DUSAGE_DEPTHSTENCIL) != 0; }
    bool autoGen() const { return (usage & D3DUSAGE_AUTOGENMIPMAP) != 0; }
    GLenum target() const { return type == 0 ? GL_TEXTURE_2D : type == 1 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_3D; }
    size_t levelBytes(UINT l) const;
    bool init(Device *d, int type, UINT w, UINT h, UINT depth, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    HRESULT lock(UINT face, UINT level, const RECT *r, const D3DBOX *b, void **bits, INT *pitch, INT *slice, DWORD flags);
    HRESULT unlock(UINT face, UINT level, DWORD flags);
    void realize();      // GL thread
    void release();      // any thread: queues GL deletion
    // GL thread, before realize. 0 = hardware or off, 1 = shader decode, 2 = shader encode (undo hardware sRGB).
    int commitSrgb(bool want);
    ~TexCore();
};

struct KTexture;
struct KCubeTexture;
struct KVolumeTexture;

// ------------------------------------------------------------------------------------------------
// Surfaces
// ------------------------------------------------------------------------------------------------
enum SurfKind : uint8_t
{
    SK_TEXLEVEL,   // a level (or cube face) of a texture
    SK_RENDERTARGET,
    SK_DEPTH,
    SK_PLAIN,      // system memory
    SK_BACKBUFFER
};

struct KSurface : Unk<IDirect3DSurface9>
{
    uint64_t gpuId = 0;
    Device *dev = nullptr;
    SurfKind kind = SK_PLAIN;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    const FmtInfo *fi = nullptr;
    UINT w = 0, h = 0;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE;
    TexCore *parent = nullptr; // SK_TEXLEVEL
    IUnknown *parentCom = nullptr;
    UINT face = 0, level = 0;
    GLuint rb = 0;
    std::vector<uint8_t> sys;
    bool locked = false;

    // IUnknown: level surfaces share their texture's lifetime.
    ULONG AddRef() override;
    ULONG Release() override;
    void OnZero() override;
    void releaseGl();
    void realizeRb();
    GLuint glTexture() const;

    // IDirect3DResource9
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_NOTFOUND; }
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD SetPriority(DWORD) override { return 0; }
    DWORD GetPriority() override { return 0; }
    void PreLoad() override {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_SURFACE; }
    // IDirect3DSurface9
    HRESULT GetContainer(REFIID, void **pp) override;
    HRESULT GetDesc(D3DSURFACE_DESC *pDesc) override;
    HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT UnlockRect() override;
    HRESULT GetDC(HDC *) override { return D3DERR_INVALIDCALL; }
    HRESULT ReleaseDC(HDC) override { return D3DERR_INVALIDCALL; }
    ~KSurface() override;
};

// ------------------------------------------------------------------------------------------------
// Buffers
// ------------------------------------------------------------------------------------------------
struct BufferCore
{
    uint64_t gpuId = 0;
    uint64_t gpuGeneration = 0;
    Device *dev = nullptr;
    UINT size = 0;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    std::vector<uint8_t> data;
    GLuint bo = 0;
    GLenum target = GL_ARRAY_BUFFER;
    UINT dirtyLo = 0xFFFFFFFFu, dirtyHi = 0;
    UINT bufferSize = 0; // size of the GL buffer store
    const uint8_t *external = nullptr; // engine pointer for one glBufferSubData; not owned
    UINT externalOff = 0, externalBytes = 0;
    std::mutex mu;
    bool locked = false;

    bool init(Device *d, UINT size, DWORD usage, D3DPOOL pool, GLenum target);
    HRESULT lock(UINT off, UINT sz, void **pp, DWORD flags);
    HRESULT unlock();
    void stageExternal(const void *src, UINT off, UINT bytes);
    GLuint realize(); // GL thread: creates the buffer / uploads the dirty range
    void release();
    ~BufferCore();
};

struct KVertexBuffer : Unk<IDirect3DVertexBuffer9>
{
    BufferCore c;
    DWORD fvf = 0;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_NOTFOUND; }
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD SetPriority(DWORD) override { return 0; }
    DWORD GetPriority() override { return 0; }
    void PreLoad() override {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT Lock(UINT o, UINT s, void **pp, DWORD f) override { return c.lock(o, s, pp, f); }
    HRESULT Unlock() override { return c.unlock(); }
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *d) override;
};

struct KIndexBuffer : Unk<IDirect3DIndexBuffer9>
{
    BufferCore c;
    D3DFORMAT fmt = D3DFMT_INDEX16;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_NOTFOUND; }
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }
    DWORD SetPriority(DWORD) override { return 0; }
    DWORD GetPriority() override { return 0; }
    void PreLoad() override {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT Lock(UINT o, UINT s, void **pp, DWORD f) override { return c.lock(o, s, pp, f); }
    HRESULT Unlock() override { return c.unlock(); }
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *d) override;
};

// ------------------------------------------------------------------------------------------------
// Declarations, shaders, queries
// ------------------------------------------------------------------------------------------------
struct KDecl : Unk<IDirect3DVertexDeclaration9>
{
    Device *dev = nullptr;
    std::vector<D3DVERTEXELEMENT9> el; // without the terminator
    uint32_t bgraMask = 0;             // attribute locations fed by D3DCOLOR (need a B/R swap in the shader)
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT GetDeclaration(D3DVERTEXELEMENT9 *e, UINT *n) override;
};

struct KVShader : Unk<IDirect3DVertexShader9>
{
    Device *dev = nullptr;
    uint64_t gpuHash = 0;
    std::vector<uint32_t> code;
    sm3::Result tr;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT GetFunction(void *p, UINT *n) override;
    void OnZero() override;
};

struct KPShader : Unk<IDirect3DPixelShader9>
{
    Device *dev = nullptr;
    uint64_t gpuHash = 0;
    std::vector<uint32_t> code;
    sm3::Result tr;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT GetFunction(void *p, UINT *n) override;
    void OnZero() override;
};

struct KQuery : Unk<IDirect3DQuery9>
{
    Device *dev = nullptr;
    D3DQUERYTYPE type = D3DQUERYTYPE_EVENT;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    D3DQUERYTYPE GetType() override { return type; }
    DWORD GetDataSize() override { return type == D3DQUERYTYPE_OCCLUSION ? 4 : 4; }
    HRESULT Issue(DWORD) override { return D3D_OK; }
    HRESULT GetData(void *p, DWORD size, DWORD) override;
};


} // namespace kd

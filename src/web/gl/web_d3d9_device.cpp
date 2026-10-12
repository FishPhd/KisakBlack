// The Direct3D 9 device and adapter over WebGL2. See web_d3d9_priv.h for the design.
#include "web_d3d9_device.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <emscripten.h>

#define KLOG(...)                                   \
    do                                              \
    {                                               \
        fprintf(stderr, "d3d9gl: " __VA_ARGS__);    \
        fprintf(stderr, "\n");                      \
    } while (0)

// Where the GL thread currently is, readable from the page while the thread is busy or stuck
// (Module._Kisak_GlPhase / _Kisak_GlDraws): the first thing to ask when the picture stops moving.
extern "C"
{
const char *volatile kisak_gl_phase = "init";
volatile unsigned kisak_gl_draws = 0;
EMSCRIPTEN_KEEPALIVE const char *Kisak_GlPhase() { return kisak_gl_phase; }
EMSCRIPTEN_KEEPALIVE unsigned Kisak_GlDraws() { return kisak_gl_draws; }
}
#define GLPHASE(x) (kisak_gl_phase = (x))

namespace kd
{
namespace
{
enum
{
    RG_BLEND = 0, RG_DEPTH, RG_STENCIL, RG_RASTER, RG_MASK, RG_SCISSOR
};

int GroupOf(D3DRENDERSTATETYPE s)
{
    switch (s)
    {
    case D3DRS_ALPHABLENDENABLE: case D3DRS_SRCBLEND: case D3DRS_DESTBLEND: case D3DRS_BLENDOP:
    case D3DRS_SEPARATEALPHABLENDENABLE: case D3DRS_SRCBLENDALPHA: case D3DRS_DESTBLENDALPHA:
    case D3DRS_BLENDOPALPHA: case D3DRS_BLENDFACTOR:
        return RG_BLEND;
    case D3DRS_ZENABLE: case D3DRS_ZWRITEENABLE: case D3DRS_ZFUNC:
        return RG_DEPTH;
    case D3DRS_STENCILENABLE: case D3DRS_STENCILFAIL: case D3DRS_STENCILZFAIL: case D3DRS_STENCILPASS:
    case D3DRS_STENCILFUNC: case D3DRS_STENCILREF: case D3DRS_STENCILMASK: case D3DRS_STENCILWRITEMASK:
    case D3DRS_TWOSIDEDSTENCILMODE: case D3DRS_CCW_STENCILFAIL: case D3DRS_CCW_STENCILZFAIL:
    case D3DRS_CCW_STENCILPASS: case D3DRS_CCW_STENCILFUNC:
        return RG_STENCIL;
    case D3DRS_CULLMODE: case D3DRS_DEPTHBIAS: case D3DRS_SLOPESCALEDEPTHBIAS:
        return RG_RASTER;
    case D3DRS_COLORWRITEENABLE:
        return RG_MASK;
    case D3DRS_SCISSORTESTENABLE:
        return RG_SCISSOR;
    default:
        return -1;
    }
}

GLenum CmpFunc(DWORD f)
{
    switch (f)
    {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

GLenum StencilOp(DWORD o)
{
    switch (o)
    {
    case D3DSTENCILOP_ZERO: return GL_ZERO;
    case D3DSTENCILOP_REPLACE: return GL_REPLACE;
    case D3DSTENCILOP_INCRSAT: return GL_INCR;
    case D3DSTENCILOP_DECRSAT: return GL_DECR;
    case D3DSTENCILOP_INVERT: return GL_INVERT;
    case D3DSTENCILOP_INCR: return GL_INCR_WRAP;
    case D3DSTENCILOP_DECR: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

GLenum BlendFactor(DWORD b)
{
    switch (b)
    {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    case D3DBLEND_BLENDFACTOR: return GL_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR: return GL_ONE_MINUS_CONSTANT_COLOR;
    default: return GL_ONE;
    }
}

GLenum BlendOp(DWORD o)
{
    switch (o)
    {
    case D3DBLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN: return GL_MIN;
    case D3DBLENDOP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

GLenum WrapMode(DWORD a)
{
    switch (a)
    {
    case D3DTADDRESS_MIRROR: case D3DTADDRESS_MIRRORONCE: return GL_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP: case D3DTADDRESS_BORDER: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

size_t PrimCount(D3DPRIMITIVETYPE t, UINT n)
{
    switch (t)
    {
    case D3DPT_POINTLIST: return n;
    case D3DPT_LINELIST: return (size_t)n * 2;
    case D3DPT_LINESTRIP: return (size_t)n + 1;
    case D3DPT_TRIANGLELIST: return (size_t)n * 3;
    default: return (size_t)n + 2;
    }
}

GLenum PrimMode(D3DPRIMITIVETYPE t)
{
    switch (t)
    {
    case D3DPT_POINTLIST: return GL_POINTS;
    case D3DPT_LINELIST: return GL_LINES;
    case D3DPT_LINESTRIP: return GL_LINE_STRIP;
    case D3DPT_TRIANGLELIST: return GL_TRIANGLES;
    case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
    default: return GL_TRIANGLE_FAN;
    }
}

struct AttrFmt
{
    GLint size;
    GLenum type;
    GLboolean norm;
};

bool DeclFmt(int t, AttrFmt &o)
{
    switch (t)
    {
    case D3DDECLTYPE_FLOAT1: o = {1, GL_FLOAT, GL_FALSE}; return true;
    case D3DDECLTYPE_FLOAT2: o = {2, GL_FLOAT, GL_FALSE}; return true;
    case D3DDECLTYPE_FLOAT3: o = {3, GL_FLOAT, GL_FALSE}; return true;
    case D3DDECLTYPE_FLOAT4: o = {4, GL_FLOAT, GL_FALSE}; return true;
    case D3DDECLTYPE_D3DCOLOR: o = {4, GL_UNSIGNED_BYTE, GL_TRUE}; return true;
    case D3DDECLTYPE_UBYTE4: o = {4, GL_UNSIGNED_BYTE, GL_FALSE}; return true;
    case D3DDECLTYPE_SHORT2: o = {2, GL_SHORT, GL_FALSE}; return true;
    case D3DDECLTYPE_SHORT4: o = {4, GL_SHORT, GL_FALSE}; return true;
    case D3DDECLTYPE_UBYTE4N: o = {4, GL_UNSIGNED_BYTE, GL_TRUE}; return true;
    case D3DDECLTYPE_SHORT2N: o = {2, GL_SHORT, GL_TRUE}; return true;
    case D3DDECLTYPE_SHORT4N: o = {4, GL_SHORT, GL_TRUE}; return true;
    case D3DDECLTYPE_USHORT2N: o = {2, GL_UNSIGNED_SHORT, GL_TRUE}; return true;
    case D3DDECLTYPE_USHORT4N: o = {4, GL_UNSIGNED_SHORT, GL_TRUE}; return true;
    case D3DDECLTYPE_UDEC3: o = {4, GL_UNSIGNED_INT_2_10_10_10_REV, GL_FALSE}; return true;
    case D3DDECLTYPE_DEC3N: o = {4, GL_INT_2_10_10_10_REV, GL_TRUE}; return true;
    case D3DDECLTYPE_FLOAT16_2: o = {2, GL_HALF_FLOAT, GL_FALSE}; return true;
    case D3DDECLTYPE_FLOAT16_4: o = {4, GL_HALF_FLOAT, GL_FALSE}; return true;
    default: return false;
    }
}

// ---------------------------------------------------------------------------------------------
// IDirect3D9
// ---------------------------------------------------------------------------------------------
Device g_device;

struct ModeEntry
{
    UINT w, h;
};
const ModeEntry kModes[] = {{800, 600},   {1024, 768},  {1152, 864},  {1280, 720},  {1280, 800},
                            {1280, 960},  {1280, 1024}, {1366, 768},  {1440, 900},  {1600, 900},
                            {1600, 1200}, {1680, 1050}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
constexpr UINT kModeCount = (UINT)(sizeof(kModes) / sizeof(kModes[0]));

struct D3D9Impl : Unk<IDirect3D9>
{
    HRESULT RegisterSoftwareDevice(void *) override { return D3DERR_NOTAVAILABLE; }
    UINT GetAdapterCount() override { return 1; }
    HRESULT GetAdapterIdentifier(UINT a, DWORD, D3DADAPTER_IDENTIFIER9 *id) override
    {
        if (a != 0)
            return D3DERR_INVALIDCALL;
        memset(id, 0, sizeof(*id));
        strcpy(id->Driver, "webgl2");
        strcpy(id->Description, "WebGL2 (Direct3D 9 emulation)");
        strcpy(id->DeviceName, "\\\\.\\DISPLAY1");
        id->DriverVersion.QuadPart = 1;
        id->VendorId = 0;
        id->DeviceId = 0;
        return D3D_OK;
    }
    UINT GetAdapterModeCount(UINT a, D3DFORMAT f) override { return (a == 0 && f == D3DFMT_X8R8G8B8) ? kModeCount : 0; }
    HRESULT EnumAdapterModes(UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE *o) override
    {
        if (a != 0 || f != D3DFMT_X8R8G8B8 || m >= kModeCount)
            return D3DERR_INVALIDCALL;
        o->Width = kModes[m].w;
        o->Height = kModes[m].h;
        o->RefreshRate = 60;
        o->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE *o) override
    {
        if (a != 0)
            return D3DERR_INVALIDCALL;
        o->Width = 1920;
        o->Height = 1080;
        o->RefreshRate = 60;
        o->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) override { return D3D_OK; }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE rtype, D3DFORMAT f) override
    {
        const FmtInfo *fi = FindFmt(f);
        if (!fi)
            return D3DERR_NOTAVAILABLE; // INTZ is in the table; RESZ, NULL, ATOC, ATI1N... are not
        if (usage & 0x100000) // vertex texture
            return D3DERR_NOTAVAILABLE;
        if (usage & D3DUSAGE_DEPTHSTENCIL)
        {
            if (!fi->depth)
                return D3DERR_NOTAVAILABLE;
            // D24S8 (and INTZ) are sampleable shadow maps. Rejecting them forces the R32F
            // fallback, which turns shadow maps off, so dynamic lights show through walls.
            return D3D_OK;
        }
        if (fi->depth && f != FMT_INTZ)
            return D3DERR_NOTAVAILABLE;
        if (usage & D3DUSAGE_RENDERTARGET)
        {
            if (!fi->colorRenderable)
                return D3DERR_NOTAVAILABLE;
            if (rtype == D3DRTYPE_VOLUMETEXTURE)
                return D3DERR_NOTAVAILABLE;
        }
        if (fi->compressed && g_device.created && g_device.gl && !GlHasExt("WEBGL_compressed_texture_s3tc"))
            return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE t, DWORD *q) override
    {
        if (q)
            *q = 1;
        return t <= D3DMULTISAMPLE_NONMASKABLE ? D3D_OK : D3DERR_NOTAVAILABLE;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT ds) override
    {
        const FmtInfo *fi = FindFmt(ds);
        return fi && fi->depth ? D3D_OK : D3DERR_NOTAVAILABLE;
    }
    HRESULT CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *c) override { return g_device.GetDeviceCaps(c); }
    HMONITOR GetAdapterMonitor(UINT) override { return (HMONITOR)(uintptr_t)1; }
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) override
    {
        if (!g_device.create(this, *pp))
            return D3DERR_NOTAVAILABLE;
        g_device.AddRef();
        *out = &g_device;
        return D3D_OK;
    }
};

struct KSwapChain : Unk<IDirect3DSwapChain9>
{
    HRESULT Present(const RECT *, const RECT *, HWND, const void *, DWORD) override { return g_device.Present(nullptr, nullptr, nullptr, nullptr); }
    HRESULT GetFrontBufferData(IDirect3DSurface9 *) override { return D3DERR_INVALIDCALL; }
    HRESULT GetBackBuffer(UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9 **pp) override { return g_device.GetBackBuffer(0, i, t, pp); }
    HRESULT GetRasterStatus(D3DRASTER_STATUS *s) override { return g_device.GetRasterStatus(0, s); }
    HRESULT GetDisplayMode(D3DDISPLAYMODE *m) override { return g_device.GetDisplayMode(0, m); }
    HRESULT GetDevice(IDirect3DDevice9 **pp) override
    {
        g_device.AddRef();
        *pp = &g_device;
        return D3D_OK;
    }
    HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS *p) override
    {
        *p = g_device.pp;
        return D3D_OK;
    }
};

template <class T> inline void SetRef(T *&slot, T *v)
{
    if (v)
        v->AddRef();
    if (slot)
        slot->Release();
    slot = v;
}
} // namespace

// ================================================================================================
// Device: construction, GL setup
// ================================================================================================
Device::Device()
{
    memset(rs, 0, sizeof(rs));
    memset(ss, 0, sizeof(ss));
    memset(vsF, 0, sizeof(vsF));
    memset(psF, 0, sizeof(psF));
    memset(vsFseq, 0, sizeof(vsFseq));
    memset(psFseq, 0, sizeof(psFseq));
    memset(vsI, 0, sizeof(vsI));
    memset(psI, 0, sizeof(psI));
    memset(vsB, 0, sizeof(vsB));
    memset(psB, 0, sizeof(psB));
    memset(&gamma, 0, sizeof(gamma));
}

static void ResetStateDefaults(Device &d)
{
    memset(d.rs, 0, sizeof(d.rs));
    d.rs[D3DRS_ZENABLE] = 1;
    d.rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    d.rs[D3DRS_ZWRITEENABLE] = 1;
    d.rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    d.rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    d.rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    d.rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    d.rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    d.rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    d.rs[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
    d.rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    d.rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    d.rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    d.rs[D3DRS_COLORWRITEENABLE] = 0xF;
    d.rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    d.rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    d.rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    d.rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    d.rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFFu;
    for (int s = 0; s < 20; ++s)
    {
        d.ss[s][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        d.ss[s][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        d.ss[s][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        d.ss[s][D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
        d.ss[s][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        d.ss[s][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        d.ss[s][D3DSAMP_MAXANISOTROPY] = 1;
    }
    d.groupDirty.set();
}

bool Device::create(IDirect3D9 *own, const D3DPRESENT_PARAMETERS &p)
{
    owner = own;
    pp = p;
    ResetStateDefaults(*this);
    for (int i = 0; i < 4; ++i)
        SetRef<KVertexBuffer>(streams[i].vb, nullptr), streams[i].off = streams[i].stride = 0;
    if (!created)
    {
        glThread = pthread_self();
        gl = GlInit((int)p.BackBufferWidth, (int)p.BackBufferHeight);
        if (gl)
            KLOG("WebGL2 context created (%ux%u)", p.BackBufferWidth, p.BackBufferHeight);
        else
            KLOG("no WebGL2 here: running the device in null mode (%ux%u)", p.BackBufferWidth, p.BackBufferHeight);
        if (gl)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGenVertexArrays(1, &defaultVao);
            glBindVertexArray(defaultVao);
            floatLinear = GlHasExt("OES_texture_float_linear");
            GLfloat maxAniso = 1.0f;
            if (GlHasExt("EXT_texture_filter_anisotropic"))
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            aniso = (int)maxAniso;
            const GLenum targets[3] = {GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_3D};
            for (int i = 0; i < 3; ++i)
            {
                glGenTextures(1, &dummyTex[i]);
                bindTextureScratch(targets[i], dummyTex[i]);
                // White, not black: an unbound sampler is sampled by lighting and detail techniques.
                // Black here turns whole materials black. White is the multiply identity.
                const uint8_t px[4] = {255, 255, 255, 255};
                if (i == 0)
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                else if (i == 1)
                    for (int f = 0; f < 6; ++f)
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                else
                    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                glTexParameteri(targets[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(targets[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                // glTexImage leaves TEXTURE_MAX_LEVEL at 1000. A mipmapped sampler then treats the
                // texture as incomplete and returns black instead of this white texel.
                glTexParameteri(targets[i], GL_TEXTURE_MAX_LEVEL, 0);
            }
            glDisable(GL_DITHER);
            glEnable(GL_DEPTH_TEST);
        }
        created = true;
    }
    else if (gl)
    {
        GlResize((int)p.BackBufferWidth, (int)p.BackBufferHeight);
    }
    createBackbuffer();
    vp = {0, 0, p.BackBufferWidth, p.BackBufferHeight, 0.0f, 1.0f};
    scissor = {0, 0, (LONG)p.BackBufferWidth, (LONG)p.BackBufferHeight};
    targetDirty = vpDirty = true;
    return true;
}

void Device::createBackbuffer()
{
    if (backBuffer)
    {
        forgetFbosFor(backBuffer);
        backBuffer->Release();
        backBuffer = nullptr;
    }
    auto *s = new KSurface();
    s->dev = this;
    s->kind = SK_BACKBUFFER;
    s->fmt = D3DFMT_A8R8G8B8;
    s->fi = FindFmt(D3DFMT_A8R8G8B8);
    s->w = pp.BackBufferWidth;
    s->h = pp.BackBufferHeight;
    s->usage = D3DUSAGE_RENDERTARGET;
    backBuffer = s;
    SetRef<KSurface>(rt[0], s);
    SetRef<KSurface>(ds, nullptr);
    for (int i = 1; i < 4; ++i)
        SetRef<KSurface>(rt[i], nullptr);
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *p)
{
    pp = *p;
    if (gl && onGl())
        GlResize((int)p->BackBufferWidth, (int)p->BackBufferHeight);
    createBackbuffer();
    vp = {0, 0, p->BackBufferWidth, p->BackBufferHeight, 0.0f, 1.0f};
    scissor = {0, 0, (LONG)p->BackBufferWidth, (LONG)p->BackBufferHeight};
    targetDirty = vpDirty = true;
    groupDirty.set();
    return D3D_OK;
}

HRESULT Device::GetDirect3D(IDirect3D9 **pp_)
{
    owner->AddRef();
    *pp_ = owner;
    return D3D_OK;
}

HRESULT Device::GetDisplayMode(UINT, D3DDISPLAYMODE *m)
{
    m->Width = 1920;
    m->Height = 1080;
    m->RefreshRate = 60;
    m->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Device::GetDeviceCaps(D3DCAPS9 *c)
{
    memset(c, 0, sizeof(*c));
    c->DeviceType = D3DDEVTYPE_HAL;
    c->Caps2 = 0x20000000;           // dynamic textures
    c->Caps3 = 0x20 | 0x100;
    c->PresentationIntervals = 0x80000001u;
    c->DevCaps = 0x8000 | 0x10000 | 0x400 | 0x80000 | 0x40 | 0x1000;
    c->PrimitiveMiscCaps = 0x2 | 0x80 | 0x800 | 0x20000 | 0x70 | 0x4;
    c->RasterCaps = 0x2000000 | 0x4000000;
    c->ZCmpCaps = 0xFF;
    c->SrcBlendCaps = 0x3FFF;
    c->DestBlendCaps = 0x3FFF;
    c->AlphaCmpCaps = 0xFF;
    c->ShadeCaps = 0x20000 | 0x8 | 0x200 | 0x2000 | 0x80000 | 0x2;
    c->TextureCaps = 0x4 | 0x800 | 0x4000 | 0x1 | 0x10000 | 0x2000 | 0x40000;
    const DWORD filt = 0x100 | 0x200 | 0x400 | 0x10000 | 0x20000 | 0x1000000 | 0x2000000 | 0x4000000;
    c->TextureFilterCaps = filt;
    c->CubeTextureFilterCaps = filt;
    c->VolumeTextureFilterCaps = filt;
    c->TextureAddressCaps = 0x3F;
    c->VolumeTextureAddressCaps = 0x3F;
    c->LineCaps = 0x1F;
    const int maxTex = gl ? std::min(GlMaxTextureSize(), 8192) : 8192;
    c->MaxTextureWidth = c->MaxTextureHeight = (DWORD)maxTex;
    c->MaxVolumeExtent = 2048;
    c->MaxTextureRepeat = 8192;
    c->MaxTextureAspectRatio = 8192;
    c->MaxAnisotropy = (DWORD)(aniso > 1 ? aniso : 16);
    c->MaxVertexW = 1e10f;
    c->GuardBandLeft = c->GuardBandTop = -8192.0f;
    c->GuardBandRight = c->GuardBandBottom = 8192.0f;
    c->StencilCaps = 0x1FF;
    c->FVFCaps = 8;
    c->TextureOpCaps = 0x03FFFFFF;
    c->MaxTextureBlendStages = 8;
    c->MaxSimultaneousTextures = 16;
    c->VertexProcessingCaps = 0x1 | 0x2 | 0x8 | 0x20 | 0x40 | 0x80 | 0x100;
    c->MaxActiveLights = 8;
    c->MaxUserClipPlanes = 6;
    c->MaxVertexBlendMatrices = 4;
    c->MaxPointSize = 64.0f;
    c->MaxPrimitiveCount = 0x3FFFFFFF;
    c->MaxVertexIndex = 0xFFFFFF;
    c->MaxStreams = 16;
    c->MaxStreamStride = 255;
    c->VertexShaderVersion = 0xFFFE0300;
    c->MaxVertexShaderConst = 256;
    c->PixelShaderVersion = 0xFFFF0300;
    c->PixelShader1xMaxValue = 1e10f;
    c->DevCaps2 = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20;
    c->DeclTypes = 0x3FF;
    c->NumSimultaneousRTs = 4;
    c->StretchRectFilterCaps = 0x3000300;
    c->VS20Caps = {1, 24, 32, 4};
    c->PS20Caps = {0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20, 24, 32, 4, 512};
    c->VertexTextureFilterCaps = 0;
    c->MaxVShaderInstructionsExecuted = 65535;
    c->MaxPShaderInstructionsExecuted = 65535;
    c->MaxVertexShader30InstructionSlots = 32768;
    c->MaxPixelShader30InstructionSlots = 32768;
    return D3D_OK;
}

void Device::SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *r)
{
    if (r)
        gamma = *r;
}

void Device::GetGammaRamp(UINT, D3DGAMMARAMP *r)
{
    *r = gamma;
}

HRESULT Device::GetSwapChain(UINT i, IDirect3DSwapChain9 **pp_)
{
    if (i != 0)
        return D3DERR_INVALIDCALL;
    *pp_ = new KSwapChain();
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **pp_)
{
    backBuffer->AddRef();
    *pp_ = backBuffer;
    return D3D_OK;
}

// ================================================================================================
// Deferred deletion and small GL helpers
// ================================================================================================
void Device::queueDelete(GlKind k, GLuint id)
{
    if (!gl || !id)
        return;
    std::lock_guard<std::mutex> g(delMu);
    dead.emplace_back(k, id);
}

void Device::flushDeletes()
{
    if (!onGl())
        return;
    std::vector<std::pair<GlKind, GLuint>> list;
    std::vector<KSurface *> surfs;
    {
        std::lock_guard<std::mutex> g(delMu);
        if (dead.empty() && deadSurf.empty())
            return;
        list.swap(dead);
        surfs.swap(deadSurf);
    }
    for (KSurface *s : surfs)
        forgetFbosNow(s);
    for (auto &d : list)
    {
        switch (d.first)
        {
        case GLK_TEXTURE:
            forgetTexture(d.second);
            glDeleteTextures(1, &d.second);
            break;
        case GLK_RENDERBUFFER: glDeleteRenderbuffers(1, &d.second); break;
        case GLK_BUFFER:
            if (curArrayBuf == d.second)
                curArrayBuf = 0;
            if (curElemBuf == d.second)
                curElemBuf = 0;
            for (AttrState &a : attr)
                if (a.buf == d.second)
                    a.buf = 0;
            glDeleteBuffers(1, &d.second);
            break;
        case GLK_PROGRAM:
            if (curProgram == d.second)
                curProgram = 0;
            glDeleteProgram(d.second);
            break;
        case GLK_FRAMEBUFFER:
            if (boundFbo == d.second)
                boundFbo = 0xFFFFFFFFu;
            glDeleteFramebuffers(1, &d.second);
            break;
        case GLK_SAMPLER: glDeleteSamplers(1, &d.second); break;
        }
    }
}

void Device::bindTextureScratch(GLenum target, GLuint tex_)
{
    const int unit = 16;
    if (activeUnit != unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        activeUnit = unit;
    }
    glBindTexture(target, tex_);
    boundTex[unit] = tex_;
    boundTarget[unit] = target;
}

void Device::forgetTexture(GLuint t)
{
    for (int i = 0; i < 34; ++i)
        if (boundTex[i] == t)
        {
            boundTex[i] = 0;
            boundTarget[i] = 0;
        }
    // FBOs attached to this texture are dropped by forgetFbosFor via the owning surfaces.
}

void Device::forgetFbosFor(KSurface *s)
{
    if (!gl)
        return;
    std::lock_guard<std::mutex> g(delMu);
    deadSurf.push_back(s);
}

void Device::forgetFbosNow(KSurface *s)
{
    for (size_t i = 0; i < fbos.size();)
    {
        if (fbos[i].c == s || fbos[i].d == s)
        {
            if (boundFbo == fbos[i].fbo)
            {
                targetDirty = true;
                boundFbo = 0xFFFFFFFFu;
            }
            glDeleteFramebuffers(1, &fbos[i].fbo);
            if (fbos[i].tmpDepth)
                glDeleteRenderbuffers(1, &fbos[i].tmpDepth);
            fbos.erase(fbos.begin() + (long)i);
        }
        else
            ++i;
    }
}

void Device::bindDrawFbo(GLuint f)
{
    if (boundFbo == f)
        return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f);
    boundFbo = f;
}

GLuint Device::fboFor(KSurface *c, KSurface *d)
{
    flushDeletes();
    for (const FboEntry &e : fbos)
        if (e.c == c && e.d == d)
            return e.fbo;
    GLuint f = 0;
    glGenFramebuffers(1, &f);
    bindDrawFbo(f);
    auto attach = [&](KSurface *s, GLenum att) {
        if (s->kind == SK_TEXLEVEL)
        {
            TexCore *t = s->parent;
            t->realize();
            const GLenum tgt = t->type == 1 ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + s->face) : GL_TEXTURE_2D;
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, att, tgt, t->tex, (GLint)s->level);
        }
        else
        {
            s->realizeRb();
            glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, att, GL_RENDERBUFFER, s->rb);
        }
    };
    if (c)
        attach(c, GL_COLOR_ATTACHMENT0);
    GLuint tmpDepth = 0;
    if (d)
    {
        const GLenum datt = (d->fi && d->fi->stencil) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        if (c && (c->w != d->w || c->h != d->h))
        {
            // WebGL requires the depth attachment to match the colour size. The private buffer is only a
            // stand-in: syncMismatchedDepth copies the overlapping region of the real depth into it before a
            // pass that tests or samples scene depth, and writes it back if that pass updates depth.
            glGenRenderbuffers(1, &tmpDepth);
            glBindRenderbuffer(GL_RENDERBUFFER, tmpDepth);
            const GLenum dfmt = d->fi ? d->fi->internalFormat : (GLenum)GL_DEPTH_COMPONENT24;
            glRenderbufferStorage(GL_RENDERBUFFER, dfmt, (GLsizei)c->w, (GLsizei)c->h);
            glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, datt, GL_RENDERBUFFER, tmpDepth);
        }
        else
            attach(d, datt);
    }
    const GLenum bufs[1] = {c ? (GLenum)GL_COLOR_ATTACHMENT0 : (GLenum)GL_NONE};
    glDrawBuffers(1, bufs);
    const GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
        KLOG("framebuffer incomplete: 0x%x (color fmt 0x%x %ux%u, depth fmt 0x%x)", st, c ? (unsigned)c->fmt : 0u, c ? c->w : 0,
             c ? c->h : 0, d ? (unsigned)d->fmt : 0u);
    fbos.push_back({c, d, f, tmpDepth});
    return f;
}

// ================================================================================================
// State setting (CPU side)
// ================================================================================================
HRESULT Device::SetRenderState(D3DRENDERSTATETYPE s, DWORD v)
{
    if ((unsigned)s >= 256)
        return D3D_OK;
    rs[s] = v;
    const int g = GroupOf(s);
    if (g >= 0)
        groupDirty.set((size_t)g);
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE s, DWORD *v)
{
    *v = (unsigned)s < 256 ? rs[s] : 0;
    return D3D_OK;
}

HRESULT Device::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE t, DWORD v)
{
    if (sampler >= 256)
        sampler = 16 + (sampler - 257);
    if (sampler >= 20 || (unsigned)t >= 16)
        return D3D_OK;
    ss[sampler][t] = v;
    return D3D_OK;
}

HRESULT Device::GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE t, DWORD *v)
{
    if (sampler >= 256)
        sampler = 16 + (sampler - 257);
    *v = (sampler < 20 && (unsigned)t < 16) ? ss[sampler][t] : 0;
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture9 *t)
{
    if (stage >= 16)
        return D3D_OK;
    IDirect3DBaseTexture9 *old = static_cast<IDirect3DBaseTexture9 *>(texCom[stage]);
    if (t)
        t->AddRef();
    texCom[stage] = t;
    tex[stage] = TexCoreOf(t);
    if (old)
        old->Release();
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, IDirect3DBaseTexture9 **pp_)
{
    if (stage >= 16 || !texCom[stage])
    {
        *pp_ = nullptr;
        return D3D_OK;
    }
    texCom[stage]->AddRef();
    *pp_ = static_cast<IDirect3DBaseTexture9 *>(texCom[stage]);
    return D3D_OK;
}

HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9 *d)
{
    SetRef<KDecl>(decl, static_cast<KDecl *>(d));
    return D3D_OK;
}

HRESULT Device::GetVertexDeclaration(IDirect3DVertexDeclaration9 **pp_)
{
    if (decl)
        decl->AddRef();
    *pp_ = decl;
    return D3D_OK;
}

HRESULT Device::SetVertexShader(IDirect3DVertexShader9 *s)
{
    SetRef<KVShader>(vs, static_cast<KVShader *>(s));
    return D3D_OK;
}

HRESULT Device::GetVertexShader(IDirect3DVertexShader9 **pp_)
{
    if (vs)
        vs->AddRef();
    *pp_ = vs;
    return D3D_OK;
}

HRESULT Device::SetPixelShader(IDirect3DPixelShader9 *s)
{
    SetRef<KPShader>(ps, static_cast<KPShader *>(s));
    return D3D_OK;
}

HRESULT Device::GetPixelShader(IDirect3DPixelShader9 **pp_)
{
    if (ps)
        ps->AddRef();
    *pp_ = ps;
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantF(UINT start, const float *data, UINT count)
{
    if (start + count > 256)
        count = start < 256 ? 256 - start : 0;
    memcpy(&vsF[start * 4], data, (size_t)count * 16);
    const uint32_t s = ++fseq;
    for (UINT i = 0; i < count; ++i)
        vsFseq[start + i] = s;
    return D3D_OK;
}

HRESULT Device::GetVertexShaderConstantF(UINT start, float *data, UINT count)
{
    if (start + count > 256)
        count = start < 256 ? 256 - start : 0;
    memcpy(data, &vsF[start * 4], (size_t)count * 16);
    return D3D_OK;
}

HRESULT Device::SetPixelShaderConstantF(UINT start, const float *data, UINT count)
{
    if (start + count > 256)
        count = start < 256 ? 256 - start : 0;
    memcpy(&psF[start * 4], data, (size_t)count * 16);
    const uint32_t s = ++fseq;
    for (UINT i = 0; i < count; ++i)
        psFseq[start + i] = s;
    return D3D_OK;
}

HRESULT Device::GetPixelShaderConstantF(UINT start, float *data, UINT count)
{
    if (start + count > 256)
        count = start < 256 ? 256 - start : 0;
    memcpy(data, &psF[start * 4], (size_t)count * 16);
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantI(UINT start, const int *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    memcpy(&vsI[start * 4], data, (size_t)count * 16);
    ++iseq;
    return D3D_OK;
}

HRESULT Device::GetVertexShaderConstantI(UINT start, int *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    memcpy(data, &vsI[start * 4], (size_t)count * 16);
    return D3D_OK;
}

HRESULT Device::SetPixelShaderConstantI(UINT start, const int *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    memcpy(&psI[start * 4], data, (size_t)count * 16);
    ++iseq;
    return D3D_OK;
}

HRESULT Device::GetPixelShaderConstantI(UINT start, int *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    memcpy(data, &psI[start * 4], (size_t)count * 16);
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantB(UINT start, const BOOL *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    for (UINT i = 0; i < count; ++i)
        vsB[start + i] = data[i] ? 1 : 0;
    ++bseq;
    return D3D_OK;
}

HRESULT Device::GetVertexShaderConstantB(UINT start, BOOL *data, UINT count)
{
    for (UINT i = 0; i < count && start + i < 16; ++i)
        data[i] = vsB[start + i];
    return D3D_OK;
}

HRESULT Device::SetPixelShaderConstantB(UINT start, const BOOL *data, UINT count)
{
    if (start + count > 16)
        count = start < 16 ? 16 - start : 0;
    for (UINT i = 0; i < count; ++i)
        psB[start + i] = data[i] ? 1 : 0;
    ++bseq;
    return D3D_OK;
}

HRESULT Device::GetPixelShaderConstantB(UINT start, BOOL *data, UINT count)
{
    for (UINT i = 0; i < count && start + i < 16; ++i)
        data[i] = psB[start + i];
    return D3D_OK;
}

HRESULT Device::SetStreamSource(UINT n, IDirect3DVertexBuffer9 *b, UINT off, UINT stride)
{
    if (n >= 4)
        return D3DERR_INVALIDCALL;
    SetRef<KVertexBuffer>(streams[n].vb, static_cast<KVertexBuffer *>(b));
    streams[n].off = off;
    streams[n].stride = stride;
    return D3D_OK;
}

HRESULT Device::GetStreamSource(UINT n, IDirect3DVertexBuffer9 **pp_, UINT *off, UINT *stride)
{
    if (n >= 4)
        return D3DERR_INVALIDCALL;
    if (streams[n].vb)
        streams[n].vb->AddRef();
    *pp_ = streams[n].vb;
    *off = streams[n].off;
    *stride = streams[n].stride;
    return D3D_OK;
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer9 *b)
{
    SetRef<KIndexBuffer>(ib, static_cast<KIndexBuffer *>(b));
    return D3D_OK;
}

HRESULT Device::GetIndices(IDirect3DIndexBuffer9 **pp_)
{
    if (ib)
        ib->AddRef();
    *pp_ = ib;
    return D3D_OK;
}

HRESULT Device::SetViewport(const D3DVIEWPORT9 *v)
{
    vp = *v;
    vpDirty = true;
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT9 *v)
{
    *v = vp;
    return D3D_OK;
}

HRESULT Device::SetScissorRect(const RECT *r)
{
    scissor = *r;
    groupDirty.set(RG_SCISSOR);
    return D3D_OK;
}

HRESULT Device::GetScissorRect(RECT *r)
{
    *r = scissor;
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD i, IDirect3DSurface9 *s)
{
    if (i >= 4)
        return D3DERR_INVALIDCALL;
    SetRef<KSurface>(rt[i], static_cast<KSurface *>(s));
    if (i == 0 && s)
    {
        KSurface *k = static_cast<KSurface *>(s);
        vp = {0, 0, k->w, k->h, 0.0f, 1.0f};
        scissor = {0, 0, (LONG)k->w, (LONG)k->h};
        vpDirty = true;
        groupDirty.set(RG_SCISSOR);
    }
    targetDirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD i, IDirect3DSurface9 **pp_)
{
    if (i >= 4 || !rt[i])
    {
        *pp_ = nullptr;
        return D3DERR_NOTFOUND;
    }
    rt[i]->AddRef();
    *pp_ = rt[i];
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9 *s)
{
    SetRef<KSurface>(ds, static_cast<KSurface *>(s));
    targetDirty = true;
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface9 **pp_)
{
    if (ds)
        ds->AddRef();
    *pp_ = ds;
    return ds ? D3D_OK : D3DERR_NOTFOUND;
}

// ================================================================================================
// State application (GL thread)
// ================================================================================================
void Device::uploadUp(GLenum target, GLuint &bo, GLsizeiptr &cap, GLsizeiptr bytes, const void *data)
{
    if (!bo)
        glGenBuffers(1, &bo);
    glBindBuffer(target, bo);
    if (bytes > cap)
    {
        // Grow once. Later draws use bufferSubData so Chrome keeps its index-validation cache.
        glBufferData(target, bytes, data, GL_DYNAMIC_DRAW);
        cap = bytes;
    }
    else
        glBufferSubData(target, 0, bytes, data);
}

uint32_t Device::shadowMaskFor(const KPShader *p) const
{
    uint32_t m = 0;
    if (!p)
        return 0;
    for (const sm3::Sampler &s : p->tr.samplers)
    {
        if (s.type != 2 || !s.proj || s.raw || s.reg < 0 || s.reg >= 16)
            continue;
        const TexCore *t = tex[s.reg];
        if (t && t->fi && t->fi->depth)
            m |= 1u << s.reg;
    }
    return m;
}

bool Device::passIgnoresSceneDepth(const Program *p, const KSurface *d) const
{
    if (rs[D3DRS_ZENABLE] || rs[D3DRS_ZWRITEENABLE] || rs[D3DRS_STENCILENABLE])
        return false;
    if (!p || !d || !d->parent || !p->ps)
        return true;
    for (const sm3::Sampler &s : p->ps->tr.samplers)
    {
        if (s.reg >= 0 && s.reg < 16 && tex[s.reg] == d->parent)
            return false;
    }
    return true;
}

void Device::writebackMismatchedDepth()
{
    if (!gl)
        return;
    bool any = false;
    for (size_t i = 0; i < fbos.size(); ++i)
    {
        if (!fbos[i].depthWriteback || !fbos[i].tmpDepth || !fbos[i].c || !fbos[i].d)
            continue;
        const GLuint src = fbos[i].fbo;
        KSurface *d = fbos[i].d;
        KSurface *c = fbos[i].c;
        const bool stencil = d->fi && d->fi->stencil;
        const GLuint dst = fboFor(nullptr, d);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
        bindDrawFbo(dst);
        const int w = (int)std::min(c->w, d->w);
        const int h = (int)std::min(c->h, d->h);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0), GL_NEAREST);
        for (FboEntry &e : fbos)
            if (e.fbo == src)
                e.depthWriteback = false;
        any = true;
    }
    if (any)
    {
        groupDirty.set(RG_SCISSOR);
        targetDirty = true;
    }
}

void Device::syncMismatchedDepth(const Program *p)
{
    KSurface *c = rt[0];
    KSurface *d = ds;
    if (!c || !d || (c->w == d->w && c->h == d->h))
        return;
    if (passIgnoresSceneDepth(p, d))
        return;
    GLuint draw = 0;
    GLuint tmp = 0;
    for (const FboEntry &e : fbos)
    {
        if (e.c == c && e.d == d)
        {
            draw = e.fbo;
            tmp = e.tmpDepth;
            break;
        }
    }
    if (!draw || !tmp)
        return;
    const GLuint src = fboFor(nullptr, d);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    bindDrawFbo(draw);
    const int w = (int)std::min(c->w, d->w);
    const int h = (int)std::min(c->h, d->h);
    const bool stencil = d->fi && d->fi->stencil && rs[D3DRS_STENCILENABLE];
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0), GL_NEAREST);
    if (rs[D3DRS_ZWRITEENABLE] || (rs[D3DRS_STENCILENABLE] && rs[D3DRS_STENCILWRITEMASK]))
    {
        for (FboEntry &e : fbos)
            if (e.fbo == draw)
                e.depthWriteback = true;
    }
    // The blit turned scissor off. This draw still needs the state applyRenderStates already sent.
    if (rs[D3DRS_SCISSORTESTENABLE])
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor.left, scissor.top, std::max<LONG>(0, scissor.right - scissor.left),
                  std::max<LONG>(0, scissor.bottom - scissor.top));
    }
    else
        glDisable(GL_SCISSOR_TEST);
    targetDirty = false;
}

void Device::applyTarget()
{
    writebackMismatchedDepth();
    KSurface *c = rt[0];
    KSurface *d = ds;
    GLuint f = fboFor(c, d);
    bindDrawFbo(f);
    targetW = c ? (int)c->w : (d ? (int)d->w : 0);
    targetH = c ? (int)c->h : (d ? (int)d->h : 0);
    targetDirty = false;
}

void Device::applyViewportScissor()
{
    glViewport((GLint)vp.X, (GLint)vp.Y, (GLsizei)vp.Width, (GLsizei)vp.Height);
    glDepthRangef(vp.MinZ, vp.MaxZ);
    vpDirty = false;
}

void Device::applyRenderStates()
{
    if (groupDirty.none())
        return;
    if (groupDirty.test(RG_BLEND))
    {
        if (rs[D3DRS_ALPHABLENDENABLE])
        {
            glEnable(GL_BLEND);
            const GLenum sc = BlendFactor(rs[D3DRS_SRCBLEND]), dc = BlendFactor(rs[D3DRS_DESTBLEND]);
            const GLenum oc = BlendOp(rs[D3DRS_BLENDOP]);
            if (rs[D3DRS_SEPARATEALPHABLENDENABLE])
            {
                glBlendFuncSeparate(sc, dc, BlendFactor(rs[D3DRS_SRCBLENDALPHA]), BlendFactor(rs[D3DRS_DESTBLENDALPHA]));
                glBlendEquationSeparate(oc, BlendOp(rs[D3DRS_BLENDOPALPHA]));
            }
            else
            {
                glBlendFunc(sc, dc);
                glBlendEquation(oc);
            }
            const DWORD bf = rs[D3DRS_BLENDFACTOR];
            glBlendColor(((bf >> 16) & 255) / 255.0f, ((bf >> 8) & 255) / 255.0f, (bf & 255) / 255.0f, ((bf >> 24) & 255) / 255.0f);
        }
        else
            glDisable(GL_BLEND);
    }
    if (groupDirty.test(RG_DEPTH))
    {
        if (rs[D3DRS_ZENABLE])
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        glDepthMask(rs[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);
        glDepthFunc(CmpFunc(rs[D3DRS_ZFUNC]));
    }
    if (groupDirty.test(RG_STENCIL))
    {
        if (rs[D3DRS_STENCILENABLE])
        {
            glEnable(GL_STENCIL_TEST);
            const bool two = rs[D3DRS_TWOSIDEDSTENCILMODE] != 0;
            glStencilFuncSeparate(GL_FRONT, CmpFunc(rs[D3DRS_STENCILFUNC]), (GLint)(rs[D3DRS_STENCILREF] & 0xFF), rs[D3DRS_STENCILMASK] & 0xFF);
            glStencilOpSeparate(GL_FRONT, StencilOp(rs[D3DRS_STENCILFAIL]), StencilOp(rs[D3DRS_STENCILZFAIL]), StencilOp(rs[D3DRS_STENCILPASS]));
            if (two)
            {
                glStencilFuncSeparate(GL_BACK, CmpFunc(rs[D3DRS_CCW_STENCILFUNC]), (GLint)(rs[D3DRS_STENCILREF] & 0xFF), rs[D3DRS_STENCILMASK] & 0xFF);
                glStencilOpSeparate(GL_BACK, StencilOp(rs[D3DRS_CCW_STENCILFAIL]), StencilOp(rs[D3DRS_CCW_STENCILZFAIL]), StencilOp(rs[D3DRS_CCW_STENCILPASS]));
            }
            else
            {
                glStencilFuncSeparate(GL_BACK, CmpFunc(rs[D3DRS_STENCILFUNC]), (GLint)(rs[D3DRS_STENCILREF] & 0xFF), rs[D3DRS_STENCILMASK] & 0xFF);
                glStencilOpSeparate(GL_BACK, StencilOp(rs[D3DRS_STENCILFAIL]), StencilOp(rs[D3DRS_STENCILZFAIL]), StencilOp(rs[D3DRS_STENCILPASS]));
            }
            glStencilMask(rs[D3DRS_STENCILWRITEMASK] & 0xFF);
        }
        else
            glDisable(GL_STENCIL_TEST);
    }
    if (groupDirty.test(RG_RASTER))
    {
        switch (rs[D3DRS_CULLMODE])
        {
        case D3DCULL_NONE: glDisable(GL_CULL_FACE); break;
        case D3DCULL_CW: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
        default: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
        }
        float bias, slope;
        memcpy(&bias, &rs[D3DRS_DEPTHBIAS], 4);
        memcpy(&slope, &rs[D3DRS_SLOPESCALEDEPTHBIAS], 4);
        if (bias != 0.0f || slope != 0.0f)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            // The engine passes DEPTHBIAS as a window-z offset (dvar * 1/65536). GL polygon-offset
            // units are LSBs of the bound depth format, so scale by that format's resolution.
            // Slope scale is already in the same units D3D and GL both use.
            float lsb = 1.0f / 16777216.0f;
            if (ds && ds->fi)
            {
                switch (ds->fmt)
                {
                case D3DFMT_D16:
                case D3DFMT_D16_LOCKABLE:
                    lsb = 1.0f / 65536.0f;
                    break;
                case D3DFMT_D32:
                case D3DFMT_D32F_LOCKABLE:
                case D3DFMT_D24FS8:
                    lsb = 1.0f / 8388608.0f;
                    break;
                default:
                    break;
                }
            }
            glPolygonOffset(slope, bias / lsb);
        }
        else
            glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (groupDirty.test(RG_MASK))
    {
        const DWORD m = rs[D3DRS_COLORWRITEENABLE];
        glColorMask((m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
    }
    if (groupDirty.test(RG_SCISSOR))
    {
        if (rs[D3DRS_SCISSORTESTENABLE])
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(scissor.left, scissor.top, std::max<LONG>(0, scissor.right - scissor.left),
                      std::max<LONG>(0, scissor.bottom - scissor.top));
        }
        else
            glDisable(GL_SCISSOR_TEST);
    }
    groupDirty.reset();
}

// ================================================================================================
// Programs
// ================================================================================================
static std::string ShaderLog(GLuint s, bool program)
{
    GLint len = 0;
    if (program)
        glGetProgramiv(s, GL_INFO_LOG_LENGTH, &len);
    else
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::string log((size_t)std::max(len, 1), 0);
    if (program)
        glGetProgramInfoLog(s, len, nullptr, &log[0]);
    else
        glGetShaderInfoLog(s, len, nullptr, &log[0]);
    return log;
}

// Compilation is started here and its result is collected later (finishProgram): with
// KHR_parallel_shader_compile the browser compiles on its own threads, and a program that is not ready
// yet simply is not drawn. A synchronous compile of one of the engine's larger shaders can take hundreds of
// milliseconds in ANGLE/D3D11, and a scene needs hundreds of them.
static GLuint StartShader(GLenum type, const std::string &src)
{
    GLuint s = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    return s;
}

// wait: block until the link finished (no parallel compile available, or the caller wants the result now).
// Returns true if the program is final (ok or failed), false if it is still building.
bool Device::finishProgram(Program *pr, bool wait)
{
    static int failures = 0;
    if (pr->ok)
        return true;
    if (!wait)
    {
        GLint done = 0;
        glGetProgramiv(pr->id, 0x91B1 /* GL_COMPLETION_STATUS_KHR */, &done);
        if (!done)
            return false;
    }
    pr->pending = false;
    const GLuint prog = pr->id;
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        if (failures++ < 16)
        {
            KLOG("program link failed: %s", ShaderLog(prog, true).c_str());
            GLint vok = 0, fok = 0;
            glGetShaderiv(pr->vsh, GL_COMPILE_STATUS, &vok);
            glGetShaderiv(pr->fsh, GL_COMPILE_STATUS, &fok);
            if (!vok)
            {
                KLOG("vertex shader: %s", ShaderLog(pr->vsh, false).c_str());
                fprintf(stderr, "%s\n", sm3::LinkVertexGlsl(pr->vs->tr, pr->ps->tr).c_str());
            }
            if (!fok)
            {
                KLOG("pixel shader: %s", ShaderLog(pr->fsh, false).c_str());
                fprintf(stderr, "%s\n", pr->ps->tr.glsl.c_str());
            }
        }
        glDeleteShader(pr->vsh);
        glDeleteShader(pr->fsh);
        glDeleteProgram(prog);
        pr->id = 0;
        return true;
    }
    glDeleteShader(pr->vsh);
    glDeleteShader(pr->fsh);
    pr->ok = true;
    glUseProgram(prog);
    curProgram = prog;
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        const GLuint block = glGetUniformBlockIndex(prog, stage ? "PixelConstants" : "VertexConstants");
        if (block != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, block, stage);
    }
    pr->at = glGetUniformLocation(prog, "u_at");
    pr->srgbTex = glGetUniformLocation(prog, "u_srgbTex");
    pr->srgbEnc = glGetUniformLocation(prog, "u_srgbEnc");
    pr->shadTex = glGetUniformLocation(prog, "u_shadTex");
    pr->swzTex = glGetUniformLocation(prog, "u_swzTex");
    pr->srgbW = glGetUniformLocation(prog, "u_srgbW");
    pr->bgra = glGetUniformLocation(prog, "u_bgra");
    char name[32];
    for (const sm3::Sampler &s : pr->ps->tr.samplers)
    {
        snprintf(name, sizeof(name), "ps_s%d", s.reg);
        const GLint loc = glGetUniformLocation(prog, name);
        if (loc >= 0 && s.reg < 16)
        {
            glUniform1i(loc, s.reg);
            pr->samp[s.reg] = loc;
        }
    }
    for (const sm3::Sampler &s : pr->vs->tr.samplers)
    {
        snprintf(name, sizeof(name), "vs_s%d", s.reg);
        const GLint loc = glGetUniformLocation(prog, name);
        if (loc >= 0 && s.reg < 4)
            glUniform1i(loc, 16 + s.reg);
    }
    for (const sm3::Input &in : pr->vs->tr.inputs)
    {
        const int loc = sm3::AttribLocation(in.usage, in.index);
        if (loc >= 0)
            pr->vsInputMask |= 1u << loc;
    }
    return true;
}

Program *Device::getProgram(KVShader *v, KPShader *p, uint32_t shadowMask)
{
    std::lock_guard<std::mutex> lk(progMu);
    const ProgKey key{v, p, shadowMask};
    auto it = programs.find(key);
    if (it != programs.end())
    {
        Program *cached = it->second;
        // A program that already linked is reused. Recreating it is what makes a surface vanish
        // again after the first parallel compile finishes.
        if (cached->ok)
            return cached;
        if (cached->pending)
            finishProgram(cached, false);
        return cached;
    }
    auto *pr = new Program();
    memset(pr->samp, 0xFF, sizeof(pr->samp));
    pr->vs = v;
    pr->ps = p;
    programs[key] = pr;
    if (!v->tr.ok || !p->tr.ok)
    {
        static int translateFails = 0;
        if (translateFails++ < 24)
            KLOG("shader translate failed (vs ok=%d ps ok=%d): %s", v->tr.ok ? 1 : 0, p->tr.ok ? 1 : 0,
                 (!v->tr.ok ? v->tr.error : p->tr.error).c_str());
        return pr;
    }
    static const bool parallel = GlHasExt("KHR_parallel_shader_compile");
    const std::string vsrc = sm3::LinkVertexGlsl(v->tr, p->tr);
    const std::string fsrc = sm3::PixelVariant(p->tr, shadowMask);
    pr->shadowMask = shadowMask;
    pr->vsh = StartShader(GL_VERTEX_SHADER, vsrc);
    pr->fsh = StartShader(GL_FRAGMENT_SHADER, fsrc);
    pr->id = glCreateProgram();
    glAttachShader(pr->id, pr->vsh);
    glAttachShader(pr->id, pr->fsh);
    glLinkProgram(pr->id);
    pr->pending = true;
    finishProgram(pr, !parallel);
    return pr;
}

void Device::releaseProgramsFor(void *shader)
{
    std::lock_guard<std::mutex> lk(progMu);
    for (auto it = programs.begin(); it != programs.end();)
    {
        if (it->first.vs == shader || it->first.ps == shader)
        {
            if (it->second->id)
                queueDelete(GLK_PROGRAM, it->second->id);
            delete it->second;
            it = programs.erase(it);
        }
        else
            ++it;
    }
}

void Device::uploadConstants(Program *p)
{
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        const bool fresh = constantBuffers[stage] == 0;
        if (fresh)
        {
            glGenBuffers(1, &constantBuffers[stage]);
            glBindBuffer(GL_UNIFORM_BUFFER, constantBuffers[stage]);
            glBufferData(GL_UNIFORM_BUFFER, 4608, nullptr, GL_DYNAMIC_DRAW);
        }
        glBindBufferBase(GL_UNIFORM_BUFFER, stage, constantBuffers[stage]);
        glBindBuffer(GL_UNIFORM_BUFFER, constantBuffers[stage]);
        const float *vals = stage ? psF : vsF;
        const uint32_t *seqs = stage ? psFseq : vsFseq;
        int hi = fresh ? 255 : -1;
        if (!fresh && constantFloatSeq[stage] != fseq)
            for (int i = 0; i < 256; ++i)
                if (seqs[i] > constantFloatSeq[stage]) hi = i;
        if (hi >= 0)
            glBufferSubData(GL_UNIFORM_BUFFER, 0, (hi + 1) * 16, vals);
        constantFloatSeq[stage] = fseq;
        if (fresh || constantIntSeq[stage] != iseq)
            glBufferSubData(GL_UNIFORM_BUFFER, 4096, 256, stage ? psI : vsI);
        constantIntSeq[stage] = iseq;
        if (fresh || constantBoolSeq[stage] != bseq)
        {
            int packed[16 * 4] = {};
            const BOOL *valsB = stage ? psB : vsB;
            for (int i = 0; i < 16; ++i) packed[i * 4] = valsB[i] != 0;
            glBufferSubData(GL_UNIFORM_BUFFER, 4352, 256, packed);
        }
        constantBoolSeq[stage] = bseq;
    }
    if (p->at >= 0)
    {
        const float f = rs[D3DRS_ALPHATESTENABLE] ? (float)rs[D3DRS_ALPHAFUNC] : 0.0f;
        const float r = (float)(rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f;
        if (p->lastAt[0] != f || p->lastAt[1] != r)
        {
            glUniform2f(p->at, f, r);
            p->lastAt[0] = f;
            p->lastAt[1] = r;
        }
    }
    if (p->srgbW >= 0)
    {
        const int w = rs[D3DRS_SRGBWRITEENABLE] ? 1 : 0;
        if (p->lastSrgbW != w)
        {
            glUniform1i(p->srgbW, w);
            p->lastSrgbW = w;
        }
    }
    if (p->bgra >= 0)
    {
        const int m = decl ? (int)decl->bgraMask : 0;
        if (p->lastBgra != m)
        {
            glUniform1i(p->bgra, m);
            p->lastBgra = m;
        }
    }
}

GLuint Device::samplerObject(int unit, bool compare)
{
    const DWORD *s = ss[unit];
    const TexCore *t = tex[unit];
    uint32_t minF = s[D3DSAMP_MINFILTER], magF = s[D3DSAMP_MAGFILTER], mipF = s[D3DSAMP_MIPFILTER];
    if (t && t->fi && t->fi->type == GL_FLOAT && !floatLinear)
        minF = magF = D3DTEXF_POINT, mipF = D3DTEXF_NONE; // 32-bit float textures are not filterable here
    if (t && t->fi && t->fi->depth)
        mipF = D3DTEXF_NONE;
    if (compare)
        mipF = D3DTEXF_NONE;
    uint64_t key = 0;
    key |= (uint64_t)(s[D3DSAMP_ADDRESSU] & 7);
    key |= (uint64_t)(s[D3DSAMP_ADDRESSV] & 7) << 3;
    key |= (uint64_t)(s[D3DSAMP_ADDRESSW] & 7) << 6;
    key |= (uint64_t)(minF & 7) << 9;
    key |= (uint64_t)(magF & 7) << 12;
    key |= (uint64_t)(mipF & 3) << 15;
    key |= (uint64_t)(compare ? 1 : 0) << 29;
    uint32_t mipLod = s[D3DSAMP_MAXMIPLEVEL] & 31;
    if (t && t->appliedMax >= t->appliedBase && t->appliedBase >= 0)
    {
        const int span = t->appliedMax - t->appliedBase;
        if (mipLod > (uint32_t)span)
            mipLod = (uint32_t)span;
    }
    key |= (uint64_t)(mipLod & 31) << 24;
    auto it = samplers.find(key);
    if (it != samplers.end())
        return it->second;
    GLuint so = 0;
    glGenSamplers(1, &so);
    glSamplerParameteri(so, GL_TEXTURE_WRAP_S, (GLint)WrapMode(s[D3DSAMP_ADDRESSU]));
    glSamplerParameteri(so, GL_TEXTURE_WRAP_T, (GLint)WrapMode(s[D3DSAMP_ADDRESSV]));
    glSamplerParameteri(so, GL_TEXTURE_WRAP_R, (GLint)WrapMode(s[D3DSAMP_ADDRESSW]));
    const bool minLin = minF >= D3DTEXF_LINEAR, magLin = magF >= D3DTEXF_LINEAR;
    GLenum minGl;
    if (mipF == D3DTEXF_NONE)
        minGl = minLin ? GL_LINEAR : GL_NEAREST;
    else if (mipF == D3DTEXF_POINT)
        minGl = minLin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else
        minGl = minLin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glSamplerParameteri(so, GL_TEXTURE_MIN_FILTER, (GLint)minGl);
    glSamplerParameteri(so, GL_TEXTURE_MAG_FILTER, magLin ? GL_LINEAR : GL_NEAREST);
    glSamplerParameterf(so, GL_TEXTURE_MIN_LOD, (float)mipLod);
    // Anisotropy is not a sampler-object pname here; it is set on the texture. Comparison is.
    if (compare)
    {
        glSamplerParameteri(so, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(so, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    else
        glSamplerParameteri(so, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    samplers[key] = so;
    return so;
}

// Codes match k_srgb in web_sm3.cpp. BGRA is swapped on upload; render-target storage is not.
static unsigned SamplerSwizzle(Swz s)
{
    switch (s)
    {
    case SWZ_L8: return 1;   // R replicated to RGB, alpha 1
    case SWZ_A8: return 2;   // alpha from R
    case SWZ_A8L8: return 3; // RGB from R, alpha from G
    default: return 0;
    }
}

void Device::bindTexturesAndSamplers(Program *p)
{
    int srgbMask = 0;
    int encMask = 0;
    int shadMask = 0;
    uint32_t swzMask = 0;
    for (const sm3::Sampler &s : p->ps->tr.samplers)
    {
        const int r = s.reg;
        if (r >= 16)
            continue;
        TexCore *t = tex[r];
        const int want = s.type == 3 ? 1 : s.type == 4 ? 2 : 0;
        GLuint id = 0;
        GLenum tgt = want == 0 ? GL_TEXTURE_2D : want == 1 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_3D;
        const bool depth = t && t->fi && t->fi->depth;
        const bool shadowSamp = (p->shadowMask & (1u << r)) != 0;
        int srgbOp = 0;
        if (t && t->type == want)
        {
            if (!depth && !shadowSamp)
                srgbOp = t->commitSrgb(ss[r][D3DSAMP_SRGBTEXTURE] != 0);
            t->realize();
            id = t->tex;
            if (id && t->fi && !depth)
                swzMask |= (uint32_t)SamplerSwizzle(t->fi->swz) << (r * 2);
        }
        if (!id)
            id = dummyTex[want];
        if (boundTex[r] != id || boundTarget[r] != tgt)
        {
            if (activeUnit != r)
            {
                glActiveTexture(GL_TEXTURE0 + r);
                activeUnit = r;
            }
            glBindTexture(tgt, id);
            boundTex[r] = id;
            boundTarget[r] = tgt;
        }
        float anisoLevel = 1.0f;
        if (t && id == t->tex && aniso > 1)
        {
            const DWORD *st = ss[r];
            const bool anisoOn = st[D3DSAMP_MINFILTER] == D3DTEXF_ANISOTROPIC || st[D3DSAMP_MAGFILTER] == D3DTEXF_ANISOTROPIC;
            if (anisoOn)
            {
                const uint32_t maxA = std::min<uint32_t>(std::max<DWORD>(st[D3DSAMP_MAXANISOTROPY], 1), (uint32_t)aniso);
                anisoLevel = (float)maxA;
            }
        }
        if (anisoLevel > 1.0f && t && id == t->tex)
        {
            // Sampler objects reject TEXTURE_MAX_ANISOTROPY_EXT, and a bound sampler replaces the
            // texture's sampling state so the texture parameter would be ignored. Put wrap, filter,
            // compare and anisotropy on the texture and leave the sampler unbound.
            if (activeUnit != r)
            {
                glActiveTexture(GL_TEXTURE0 + r);
                activeUnit = r;
            }
            const DWORD *st = ss[r];
            glTexParameteri(tgt, GL_TEXTURE_WRAP_S, (GLint)WrapMode(st[D3DSAMP_ADDRESSU]));
            glTexParameteri(tgt, GL_TEXTURE_WRAP_T, (GLint)WrapMode(st[D3DSAMP_ADDRESSV]));
            glTexParameteri(tgt, GL_TEXTURE_WRAP_R, (GLint)WrapMode(st[D3DSAMP_ADDRESSW]));
            uint32_t minF = st[D3DSAMP_MINFILTER], magF = st[D3DSAMP_MAGFILTER], mipF = st[D3DSAMP_MIPFILTER];
            if (t->fi && t->fi->type == GL_FLOAT && !floatLinear)
                minF = magF = D3DTEXF_POINT, mipF = D3DTEXF_NONE;
            if ((t->fi && t->fi->depth) || shadowSamp)
                mipF = D3DTEXF_NONE;
            const bool minLin = minF >= D3DTEXF_LINEAR, magLin = magF >= D3DTEXF_LINEAR;
            GLenum minGl;
            if (mipF == D3DTEXF_NONE)
                minGl = minLin ? GL_LINEAR : GL_NEAREST;
            else if (mipF == D3DTEXF_POINT)
                minGl = minLin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
            else
                minGl = minLin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
            glTexParameteri(tgt, GL_TEXTURE_MIN_FILTER, (GLint)minGl);
            glTexParameteri(tgt, GL_TEXTURE_MAG_FILTER, magLin ? GL_LINEAR : GL_NEAREST);
            glTexParameterf(tgt, GL_TEXTURE_MIN_LOD, (float)(st[D3DSAMP_MAXMIPLEVEL] & 31));
            if (shadowSamp)
            {
                glTexParameteri(tgt, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glTexParameteri(tgt, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            }
            else
                glTexParameteri(tgt, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glTexParameterf(tgt, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisoLevel);
            t->appliedAniso = anisoLevel;
            if (boundSampler[r] != 0)
            {
                glBindSampler((GLuint)r, 0);
                boundSampler[r] = 0;
            }
        }
        else
        {
            if (t && id == t->tex && aniso > 1 && t->appliedAniso != 1.0f)
            {
                if (activeUnit != r)
                {
                    glActiveTexture(GL_TEXTURE0 + r);
                    activeUnit = r;
                }
                glTexParameterf(tgt, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
                t->appliedAniso = 1.0f;
            }
            const GLuint so = samplerObject(r, shadowSamp);
            if (boundSampler[r] != so)
            {
                glBindSampler((GLuint)r, so);
                boundSampler[r] = so;
            }
        }
        if (!shadowSamp && depth && s.proj && s.raw)
            shadMask |= 1 << r;
        else if (!shadowSamp && !depth && srgbOp == 1)
            srgbMask |= 1 << r;
        else if (!shadowSamp && !depth && srgbOp == 2)
            encMask |= 1 << r;
    }
    if (p->srgbTex >= 0 && p->lastSrgbTex != srgbMask)
    {
        glUniform1i(p->srgbTex, srgbMask);
        p->lastSrgbTex = srgbMask;
    }
    if (p->srgbEnc >= 0 && p->lastSrgbEnc != encMask)
    {
        glUniform1i(p->srgbEnc, encMask);
        p->lastSrgbEnc = encMask;
    }
    if (p->shadTex >= 0 && p->lastShadTex != shadMask)
    {
        glUniform1i(p->shadTex, shadMask);
        p->lastShadTex = shadMask;
    }
    if (p->swzTex >= 0 && (!p->swzValid || p->lastSwzTex != swzMask))
    {
        glUniform1ui(p->swzTex, (GLuint)swzMask);
        p->lastSwzTex = swzMask;
        p->swzValid = true;
    }
}

void Device::setupAttributes(int baseVertex, const KDecl *d, Program *p)
{
    uint32_t provided = 0;
    for (const D3DVERTEXELEMENT9 &e : d->el)
    {
        const int loc = sm3::AttribLocation(e.Usage, e.UsageIndex);
        if (loc < 0 || !(p->vsInputMask & (1u << loc)) || e.Stream >= 4)
            continue;
        AttrFmt f;
        if (!DeclFmt(e.Type, f))
            continue;
        const Stream &st = streams[e.Stream];
        if (!st.vb)
            continue;
        const GLuint bo = st.vb->c.realize();
        if (!bo)
            continue;
        const int64_t off = (int64_t)st.off + e.Offset + (int64_t)baseVertex * (int64_t)st.stride;
        if (off < 0)
            continue;
        AttrState &a = attr[loc];
        if (!a.enabled)
        {
            glEnableVertexAttribArray((GLuint)loc);
            a.enabled = true;
        }
        if (a.buf != bo || a.off != (GLintptr)off || a.stride != (GLsizei)st.stride || a.type != f.type || a.size != f.size ||
            a.norm != f.norm)
        {
            if (curArrayBuf != bo)
            {
                glBindBuffer(GL_ARRAY_BUFFER, bo);
                curArrayBuf = bo;
            }
            glVertexAttribPointer((GLuint)loc, f.size, f.type, f.norm, (GLsizei)st.stride, (const void *)(uintptr_t)off);
            a.buf = bo;
            a.off = (GLintptr)off;
            a.stride = (GLsizei)st.stride;
            a.type = f.type;
            a.size = f.size;
            a.norm = f.norm;
        }
        provided |= 1u << loc;
    }
    for (int loc = 0; loc < 16; ++loc)
    {
        if (provided & (1u << loc))
            continue;
        AttrState &a = attr[loc];
        if (a.enabled)
        {
            glDisableVertexAttribArray((GLuint)loc);
            a.enabled = false;
            a.buf = 0;
        }
        if (p->vsInputMask & (1u << loc))
            glVertexAttrib4f((GLuint)loc, 0.0f, 0.0f, 0.0f, 1.0f);
    }
}

bool Device::prepareDraw(bool)
{
    if (!gl)
        return false;
    if (!onGl())
    {
        if (!warnedThread)
        {
            warnedThread = true;
            KLOG("draw issued from a thread other than the GL thread; dropped");
        }
        return false;
    }
    GLPHASE("draw: flushDeletes");
    flushDeletes();
    if (!vs || !ps || !decl)
        return false;
    GLPHASE("draw: applyTarget");
    if (targetDirty)
        applyTarget();
    if (vpDirty)
        applyViewportScissor();
    GLPHASE("draw: applyRenderStates");
    applyRenderStates();
    GLPHASE("draw: getProgram (compile/link)");
    drawProg = getProgram(vs, ps, shadowMaskFor(ps));
    Program *p = drawProg;
    if (!p->ok)
    {
        ++skippedDraws;
        return false;
    }
    syncMismatchedDepth(p);
    if (curProgram != p->id)
    {
        glUseProgram(p->id);
        curProgram = p->id;
    }
    GLPHASE("draw: uploadConstants");
    uploadConstants(p);
    GLPHASE("draw: textures/samplers");
    bindTexturesAndSamplers(p);
    GLPHASE("draw: submit");
    ++kisak_gl_draws;
    return true;
}

// ================================================================================================
// Draw / clear
// ================================================================================================
HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count)
{
    if (!prepareDraw(false))
        return D3D_OK;
    Program *p = drawProg;
    setupAttributes(0, decl, p);
    glDrawArrays(PrimMode(t), (GLint)start, (GLsizei)PrimCount(t, count));
    ++drawCalls;
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT, UINT, UINT startIndex, UINT count)
{
    if (!ib || !prepareDraw(true))
        return D3D_OK;
    Program *p = drawProg;
    setupAttributes(base, decl, p);
    const GLuint ibo = ib->c.realize();
    if (!ibo)
        return D3D_OK;
    if (curElemBuf != ibo)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        curElemBuf = ibo;
    }
    const bool wide = ib->fmt == D3DFMT_INDEX32;
    glDrawElements(PrimMode(t), (GLsizei)PrimCount(t, count), wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                   (const void *)(uintptr_t)((size_t)startIndex * (wide ? 4 : 2)));
    ++drawCalls;
    return D3D_OK;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT count, const void *data, UINT stride)
{
    if (!prepareDraw(false))
        return D3D_OK;
    Program *p = drawProg;
    const size_t verts = PrimCount(t, count);
    UpSlot &slot = upRing[upRingPos];
    upRingPos = (upRingPos + 1) % 4;
    uploadUp(GL_ARRAY_BUFFER, slot.vbo, slot.vCap, (GLsizeiptr)(verts * stride), data);
    curArrayBuf = slot.vbo;
    // Bind stream 0 to the temporary buffer by hand.
    uint32_t provided = 0;
    for (const D3DVERTEXELEMENT9 &e : decl->el)
    {
        const int loc = sm3::AttribLocation(e.Usage, e.UsageIndex);
        AttrFmt f;
        if (loc < 0 || !(p->vsInputMask & (1u << loc)) || e.Stream != 0 || !DeclFmt(e.Type, f))
            continue;
        AttrState &a = attr[loc];
        if (!a.enabled)
        {
            glEnableVertexAttribArray((GLuint)loc);
            a.enabled = true;
        }
        glVertexAttribPointer((GLuint)loc, f.size, f.type, f.norm, (GLsizei)stride, (const void *)(uintptr_t)e.Offset);
        a.buf = 0; // force a re-point on the next normal draw
        provided |= 1u << loc;
    }
    for (int loc = 0; loc < 16; ++loc)
        if (!(provided & (1u << loc)) && attr[loc].enabled)
        {
            glDisableVertexAttribArray((GLuint)loc);
            attr[loc].enabled = false;
        }
    glDrawArrays(PrimMode(t), 0, (GLsizei)verts);
    ++drawCalls;
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT, UINT numVerts, UINT count, const void *idx, D3DFORMAT ifmt,
                                       const void *data, UINT stride)
{
    if (!prepareDraw(true))
        return D3D_OK;
    Program *p = drawProg;
    const size_t idxCount = PrimCount(t, count);
    const bool wide = ifmt == D3DFMT_INDEX32;
    UpSlot &slot = upRing[upRingPos];
    upRingPos = (upRingPos + 1) % 4;
    uploadUp(GL_ARRAY_BUFFER, slot.vbo, slot.vCap, (GLsizeiptr)((size_t)numVerts * stride), data);
    curArrayBuf = slot.vbo;
    uint32_t provided = 0;
    for (const D3DVERTEXELEMENT9 &e : decl->el)
    {
        const int loc = sm3::AttribLocation(e.Usage, e.UsageIndex);
        AttrFmt f;
        if (loc < 0 || !(p->vsInputMask & (1u << loc)) || e.Stream != 0 || !DeclFmt(e.Type, f))
            continue;
        AttrState &a = attr[loc];
        if (!a.enabled)
        {
            glEnableVertexAttribArray((GLuint)loc);
            a.enabled = true;
        }
        glVertexAttribPointer((GLuint)loc, f.size, f.type, f.norm, (GLsizei)stride, (const void *)(uintptr_t)e.Offset);
        a.buf = 0;
        provided |= 1u << loc;
    }
    for (int loc = 0; loc < 16; ++loc)
        if (!(provided & (1u << loc)) && attr[loc].enabled)
        {
            glDisableVertexAttribArray((GLuint)loc);
            attr[loc].enabled = false;
        }
    uploadUp(GL_ELEMENT_ARRAY_BUFFER, slot.ibo, slot.iCap, (GLsizeiptr)(idxCount * (wide ? 4 : 2)), idx);
    curElemBuf = slot.ibo;
    glDrawElements(PrimMode(t), (GLsizei)idxCount, wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
    ++drawCalls;
    return D3D_OK;
}

HRESULT Device::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    GLPHASE("clear");
    if (!gl || !onGl())
        return D3D_OK;
    flushDeletes();
    if (targetDirty)
        applyTarget();
    if (vpDirty)
        applyViewportScissor();
    GLbitfield mask = 0;
    const bool haveDepth = ds != nullptr;
    if ((flags & D3DCLEAR_TARGET) && rt[0])
    {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f,
                     ((color >> 24) & 255) / 255.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if ((flags & D3DCLEAR_ZBUFFER) && haveDepth)
    {
        glDepthMask(GL_TRUE);
        glClearDepthf(z);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if ((flags & D3DCLEAR_STENCIL) && haveDepth && ds->fi && ds->fi->stencil)
    {
        glStencilMask(0xFF);
        glClearStencil((GLint)stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    const bool depthMismatch = ds && rt[0] && (rt[0]->w != ds->w || rt[0]->h != ds->h);
    const GLbitfield depthBits = mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (depthMismatch && depthBits)
    {
        // The draw target is holding a private depth of the colour size. D3D clears the real surface.
        const GLuint colorFbo = boundFbo;
        const GLuint real = fboFor(nullptr, ds);
        bindDrawFbo(real);
        glEnable(GL_SCISSOR_TEST);
        if (count && rects)
        {
            for (DWORD i = 0; i < count; ++i)
            {
                const LONG x0 = std::max<LONG>(rects[i].x1, 0), y0 = std::max<LONG>(rects[i].y1, 0);
                glScissor(x0, y0, std::max<LONG>(rects[i].x2 - x0, 0), std::max<LONG>(rects[i].y2 - y0, 0));
                glClear(depthBits);
            }
        }
        else
        {
            glScissor((GLint)vp.X, (GLint)vp.Y, (GLsizei)vp.Width, (GLsizei)vp.Height);
            glClear(depthBits);
        }
        for (FboEntry &e : fbos)
            if (e.d == ds)
                e.depthWriteback = false;
        if (colorFbo != 0xFFFFFFFFu && colorFbo != real)
            bindDrawFbo(colorFbo);
    }
    if (mask)
    {
        glEnable(GL_SCISSOR_TEST);
        if (count && rects)
        {
            for (DWORD i = 0; i < count; ++i)
            {
                const LONG x0 = std::max<LONG>(rects[i].x1, 0), y0 = std::max<LONG>(rects[i].y1, 0);
                glScissor(x0, y0, std::max<LONG>(rects[i].x2 - x0, 0), std::max<LONG>(rects[i].y2 - y0, 0));
                glClear(mask);
            }
        }
        else
        {
            glScissor((GLint)vp.X, (GLint)vp.Y, (GLsizei)vp.Width, (GLsizei)vp.Height);
            glClear(mask);
        }
        groupDirty.set(RG_SCISSOR);
        groupDirty.set(RG_MASK);
        groupDirty.set(RG_DEPTH);
        groupDirty.set(RG_STENCIL);
    }
    return D3D_OK;
}

// ================================================================================================
// Copies
// ================================================================================================
static RECT FullRect(const KSurface *s)
{
    return {0, 0, (LONG)s->w, (LONG)s->h};
}

void Device::blit(KSurface *src, const RECT *sr, KSurface *dst, const RECT *dr, D3DTEXTUREFILTERTYPE filter)
{
    const bool depth = src->fi && src->fi->depth;
    GLuint readFbo = depth ? fboFor(nullptr, src) : fboFor(src, nullptr);
    GLuint drawFbo = (dst->fi && dst->fi->depth) ? fboFor(nullptr, dst) : fboFor(dst, nullptr);
    const RECT s = sr ? *sr : FullRect(src);
    const RECT d = dr ? *dr : FullRect(dst);
    glDisable(GL_SCISSOR_TEST);
    groupDirty.set(RG_SCISSOR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    bindDrawFbo(drawFbo);
    if (!depth)
    {
        GLenum rb[1] = {GL_COLOR_ATTACHMENT0};
        glReadBuffer(rb[0]);
    }
    glBlitFramebuffer(s.left, s.top, s.right, s.bottom, d.left, d.top, d.right, d.bottom,
                      depth ? (GL_DEPTH_BUFFER_BIT | (src->fi->stencil && dst->fi && dst->fi->stencil ? GL_STENCIL_BUFFER_BIT : 0))
                            : GL_COLOR_BUFFER_BIT,
                      (depth || filter == D3DTEXF_POINT || filter == D3DTEXF_NONE) ? GL_NEAREST : GL_LINEAR);
    targetDirty = true;
}

HRESULT Device::StretchRect(IDirect3DSurface9 *src_, const RECT *sr, IDirect3DSurface9 *dst_, const RECT *dr, D3DTEXTUREFILTERTYPE filter)
{
    GLPHASE("stretchrect");
    if (!gl || !onGl())
        return D3D_OK;
    auto *src = static_cast<KSurface *>(src_);
    auto *dst = static_cast<KSurface *>(dst_);
    if (!src || !dst || src->kind == SK_PLAIN || dst->kind == SK_PLAIN)
        return D3DERR_INVALIDCALL;
    blit(src, sr, dst, dr, filter);
    return D3D_OK;
}

HRESULT Device::ColorFill(IDirect3DSurface9 *s_, const RECT *r, D3DCOLOR color)
{
    auto *s = static_cast<KSurface *>(s_);
    if (!s)
        return D3DERR_INVALIDCALL;
    if (s->kind == SK_PLAIN)
    {
        if (!s->fi || s->fi->cpuBytes != 4)
            return D3D_OK;
        const size_t pitch = LevelPitch(*s->fi, s->w);
        if (s->sys.size() != pitch * s->h)
            s->sys.assign(pitch * s->h, 0);
        const RECT rc = r ? *r : FullRect(s);
        for (LONG y = rc.top; y < rc.bottom; ++y)
            for (LONG x = rc.left; x < rc.right; ++x)
                memcpy(&s->sys[(size_t)y * pitch + (size_t)x * 4], &color, 4);
        return D3D_OK;
    }
    if (!gl || !onGl() || (s->fi && s->fi->depth))
        return D3D_OK;
    glDisable(GL_SCISSOR_TEST);
    const GLuint f = fboFor(s, nullptr);
    bindDrawFbo(f);
    const RECT rc = r ? *r : FullRect(s);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f, ((color >> 24) & 255) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    groupDirty.set(RG_SCISSOR);
    groupDirty.set(RG_MASK);
    targetDirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTargetData(IDirect3DSurface9 *rt_, IDirect3DSurface9 *dst_)
{
    auto *src = static_cast<KSurface *>(rt_);
    auto *dst = static_cast<KSurface *>(dst_);
    if (!src || !dst || dst->kind != SK_PLAIN || !dst->fi)
        return D3DERR_INVALIDCALL;
    const size_t pitch = LevelPitch(*dst->fi, dst->w);
    dst->sys.assign(pitch * dst->h, 0);
    if (!gl || !onGl() || !src->fi)
        return D3D_OK;
    const GLuint f = fboFor(src, nullptr);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, f);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    const UINT w = std::min(src->w, dst->w), h = std::min(src->h, dst->h);
    const bool rgba8 = src->fi->type == GL_UNSIGNED_BYTE && src->fi->format == GL_RGBA && src->fi->swz != SWZ_NONE;
    if (rgba8 && dst->fi->cpuBytes == 4)
    {
        std::vector<uint8_t> tmp((size_t)w * h * 4);
        glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
        for (UINT y = 0; y < h; ++y)
        {
            uint8_t *d = &dst->sys[(size_t)y * pitch];
            const uint8_t *s = &tmp[(size_t)y * w * 4];
            for (UINT x = 0; x < w; ++x)
            {
                d[x * 4 + 0] = s[x * 4 + 2];
                d[x * 4 + 1] = s[x * 4 + 1];
                d[x * 4 + 2] = s[x * 4 + 0];
                d[x * 4 + 3] = s[x * 4 + 3];
            }
        }
    }
    else if (src->fi->cpuBytes == dst->fi->cpuBytes && !src->fi->compressed)
    {
        std::vector<uint8_t> tmp((size_t)w * h * src->fi->cpuBytes);
        glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, src->fi->format, src->fi->type, tmp.data());
        for (UINT y = 0; y < h; ++y)
            memcpy(&dst->sys[(size_t)y * pitch], &tmp[(size_t)y * w * src->fi->cpuBytes], (size_t)w * src->fi->cpuBytes);
    }
    return D3D_OK;
}

HRESULT Device::UpdateSurface(IDirect3DSurface9 *src_, const RECT *sr, IDirect3DSurface9 *dst_, const POINT *dp)
{
    auto *src = static_cast<KSurface *>(src_);
    auto *dst = static_cast<KSurface *>(dst_);
    if (!src || !dst || src->kind != SK_PLAIN || !src->fi || src->sys.empty())
        return D3DERR_INVALIDCALL;
    if (dst->kind != SK_TEXLEVEL || !dst->parent)
        return D3DERR_INVALIDCALL;
    const RECT s = sr ? *sr : FullRect(src);
    const POINT d = dp ? *dp : POINT{0, 0};
    const FmtInfo &fi = *src->fi;
    const size_t srcPitch = LevelPitch(fi, src->w);
    RECT dstRect = {d.x, d.y, d.x + (s.right - s.left), d.y + (s.bottom - s.top)};
    D3DLOCKED_RECT lr;
    if (dst->LockRect(&lr, &dstRect, 0) != D3D_OK)
        return D3DERR_INVALIDCALL;
    const size_t rowBytes = fi.compressed ? (size_t)((s.right - s.left + 3) / 4) * fi.cpuBytes : (size_t)(s.right - s.left) * fi.cpuBytes;
    const LONG rows = (LONG)LevelRows(fi, (UINT)(s.bottom - s.top));
    const size_t srcOff = fi.compressed ? (size_t)(s.top / 4) * srcPitch + (size_t)(s.left / 4) * fi.cpuBytes
                                        : (size_t)s.top * srcPitch + (size_t)s.left * fi.cpuBytes;
    for (LONG y = 0; y < rows; ++y)
        memcpy((uint8_t *)lr.pBits + (size_t)y * (size_t)lr.Pitch, &src->sys[srcOff + (size_t)y * srcPitch], rowBytes);
    dst->UnlockRect();
    return D3D_OK;
}

HRESULT Device::UpdateTexture(IDirect3DBaseTexture9 *src_, IDirect3DBaseTexture9 *dst_)
{
    TexCore *s = TexCoreOf(src_), *d = TexCoreOf(dst_);
    if (!s || !d || s->type != d->type)
        return D3DERR_INVALIDCALL;
    std::lock_guard<std::mutex> g1(s->mu);
    std::lock_guard<std::mutex> g2(d->mu);
    const UINT skip = s->levels > d->levels ? s->levels - d->levels : 0;
    for (int face = 0; face < d->faces(); ++face)
        for (UINT l = 0; l < d->levels; ++l)
        {
            const Level &sl = s->lv[(size_t)face * s->levels + l + skip];
            Level &dl = d->lv[(size_t)face * d->levels + l];
            if (sl.data.empty() || sl.data.size() != d->levelBytes(l))
                continue;
            dl.data = sl.data;
            dl.dirty = true;
            d->anyDirty = true;
        }
    return D3D_OK;
}

// ================================================================================================
// Present
// ================================================================================================
HRESULT Device::Present(const RECT *, const RECT *, HWND, const void *)
{
    ++frame;
    if (!gl || !onGl())
        return D3D_OK;
    GLPHASE("present");
    flushDeletes();
    tickShaderWarmup();
    presentBackbuffer();
    GLPHASE("between frames");
    if ((frame % 600) == 0)
        KLOG("frame %llu: %llu draws (%llu skipped, no program), %zu programs, %zu fbos", (unsigned long long)frame,
             (unsigned long long)drawCalls, (unsigned long long)skippedDraws, programs.size(), fbos.size());
    return D3D_OK;
}

void Device::presentBackbuffer()
{
    if (!backBuffer)
        return;
    const GLuint f = fboFor(backBuffer, nullptr);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, f);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    boundFbo = 0xFFFFFFFFu;
    const GLint w = (GLint)backBuffer->w, h = (GLint)backBuffer->h;
    glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GlPresent();
    groupDirty.set(RG_SCISSOR);
    groupDirty.set(RG_MASK);
    targetDirty = true;
}

} // namespace kd

// ================================================================================================
// Entry point used by the engine
// ================================================================================================
IDirect3D9 *Direct3DCreate9(UINT)
{
    return new kd::D3D9Impl();
}

// The emulated IDirect3DDevice9. See web_d3d9_priv.h for the threading model.
#pragma once
#include "web_d3d9_priv.h"

#include <pthread.h>

#include <bitset>

namespace kd
{
struct Program
{
    GLuint id = 0;
    bool ok = false;
    bool pending = false; // linking asynchronously (KHR_parallel_shader_compile); not drawn until ready
    GLuint vsh = 0, fsh = 0;
    KVShader *vs = nullptr;
    KPShader *ps = nullptr;
    GLint vc = -1, pc = -1, vi = -1, pi = -1, vb = -1, pb = -1, at = -1, srgbTex = -1, srgbEnc = -1, shadTex = -1, srgbW = -1, bgra = -1, swzTex = -1;
    GLint samp[32];
    uint32_t shadowMask = 0; // sampler registers compiled as sampler2DShadow
    uint32_t seqV = 0, seqP = 0, seqI = 0, seqB = 0; // constant sequence numbers already uploaded
    bool vcUploaded = false, pcUploaded = false;    // a full vc/pc array has been sent to this program
    float lastAt[2] = {-1.0f, -1.0f};
    int lastSrgbTex = -1, lastSrgbEnc = -1, lastShadTex = -1, lastSrgbW = -1, lastBgra = -1;
    uint32_t lastSwzTex = 0;
    bool swzValid = false;
    uint32_t vsInputMask = 0; // attribute locations the vertex shader reads
};

struct FboEntry
{
    KSurface *c;
    KSurface *d;
    GLuint fbo;
    GLuint tmpDepth = 0; // size-matched stand-in when the depth surface is a different size
    bool depthWriteback = false; // private depth was tested with ZWRITE; copy it back before the real depth is used
};

struct ProgKey
{
    void *vs;
    void *ps;
    uint32_t shadow;
    bool operator<(const ProgKey &o) const
    {
        if (vs != o.vs)
            return vs < o.vs;
        if (ps != o.ps)
            return ps < o.ps;
        return shadow < o.shadow;
    }
};

struct AttrState
{
    GLuint buf = 0;
    GLintptr off = 0;
    GLsizei stride = 0;
    GLenum type = 0;
    GLint size = 0;
    GLboolean norm = 0;
    bool enabled = false;
};

class Device final : public IDirect3DDevice9
{
public:
    Device();

    // ---- IUnknown (the device is a process-lifetime singleton; Release never frees it)
    HRESULT QueryInterface(REFIID, void **pp) override
    {
        if (pp)
            *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return (ULONG)++rc; }
    ULONG Release() override { return (ULONG)--rc; }

    // ---- IDirect3DDevice9
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    UINT GetAvailableTextureMem() override { return 0x40000000u; }
    HRESULT EvictManagedResources() override { return D3D_OK; }
    HRESULT GetDirect3D(IDirect3D9 **pp) override;
    HRESULT GetDeviceCaps(D3DCAPS9 *c) override;
    HRESULT GetDisplayMode(UINT, D3DDISPLAYMODE *m) override;
    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetCursorProperties(UINT, UINT, IDirect3DSurface9 *) override { return D3D_OK; }
    void SetCursorPosition(int, int, DWORD) override {}
    BOOL ShowCursor(BOOL) override { return FALSE; }
    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **) override { return D3DERR_NOTAVAILABLE; }
    HRESULT GetSwapChain(UINT i, IDirect3DSwapChain9 **pp) override;
    UINT GetNumberOfSwapChains() override { return 1; }
    HRESULT Reset(D3DPRESENT_PARAMETERS *p) override;
    HRESULT Present(const RECT *, const RECT *, HWND, const void *) override;
    HRESULT GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **pp) override;
    HRESULT GetRasterStatus(UINT, D3DRASTER_STATUS *s) override
    {
        s->InVBlank = FALSE;
        s->ScanLine = 0;
        return D3D_OK;
    }
    HRESULT SetDialogBoxMode(BOOL) override { return D3D_OK; }
    void SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *r) override;
    void GetGammaRamp(UINT, D3DGAMMARAMP *r) override;
    HRESULT CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *) override;
    HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **, HANDLE *) override;
    HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9 **, HANDLE *) override;
    HRESULT CreateVertexBuffer(UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9 **, HANDLE *) override;
    HRESULT CreateIndexBuffer(UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9 **, HANDLE *) override;
    HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE *) override;
    HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE *) override;
    HRESULT UpdateSurface(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const POINT *) override;
    HRESULT UpdateTexture(IDirect3DBaseTexture9 *, IDirect3DBaseTexture9 *) override;
    HRESULT GetRenderTargetData(IDirect3DSurface9 *, IDirect3DSurface9 *) override;
    HRESULT GetFrontBufferData(UINT, IDirect3DSurface9 *) override { return D3DERR_INVALIDCALL; }
    HRESULT StretchRect(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *, D3DTEXTUREFILTERTYPE) override;
    HRESULT ColorFill(IDirect3DSurface9 *, const RECT *, D3DCOLOR) override;
    HRESULT CreateOffscreenPlainSurface(UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9 **, HANDLE *) override;
    HRESULT SetRenderTarget(DWORD, IDirect3DSurface9 *) override;
    HRESULT GetRenderTarget(DWORD, IDirect3DSurface9 **) override;
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *) override;
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **) override;
    HRESULT BeginScene() override { return D3D_OK; }
    HRESULT EndScene() override { return D3D_OK; }
    HRESULT Clear(DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD) override;
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { return D3D_OK; }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE, D3DMATRIX *) override { return D3DERR_INVALIDCALL; }
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { return D3D_OK; }
    HRESULT SetViewport(const D3DVIEWPORT9 *) override;
    HRESULT GetViewport(D3DVIEWPORT9 *) override;
    HRESULT SetMaterial(const D3DMATERIAL9 *) override { return D3D_OK; }
    HRESULT GetMaterial(D3DMATERIAL9 *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetLight(DWORD, const D3DLIGHT9 *) override { return D3D_OK; }
    HRESULT GetLight(DWORD, D3DLIGHT9 *) override { return D3DERR_INVALIDCALL; }
    HRESULT LightEnable(DWORD, BOOL) override { return D3D_OK; }
    HRESULT GetLightEnable(DWORD, BOOL *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetClipPlane(DWORD, const float *) override { return D3D_OK; }
    HRESULT GetClipPlane(DWORD, float *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetRenderState(D3DRENDERSTATETYPE, DWORD) override;
    HRESULT GetRenderState(D3DRENDERSTATETYPE, DWORD *) override;
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **) override { return D3DERR_NOTAVAILABLE; }
    HRESULT BeginStateBlock() override { return D3DERR_NOTAVAILABLE; }
    HRESULT EndStateBlock(IDirect3DStateBlock9 **) override { return D3DERR_NOTAVAILABLE; }
    HRESULT SetClipStatus(const D3DCLIPSTATUS9 *) override { return D3D_OK; }
    HRESULT GetClipStatus(D3DCLIPSTATUS9 *) override { return D3DERR_INVALIDCALL; }
    HRESULT GetTexture(DWORD, IDirect3DBaseTexture9 **) override;
    HRESULT SetTexture(DWORD, IDirect3DBaseTexture9 *) override;
    HRESULT GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD *v) override
    {
        *v = 0;
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override { return D3D_OK; }
    HRESULT GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD *) override;
    HRESULT SetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD) override;
    HRESULT ValidateDevice(DWORD *n) override
    {
        *n = 1;
        return D3D_OK;
    }
    HRESULT SetPaletteEntries(UINT, const PALETTEENTRY *) override { return D3D_OK; }
    HRESULT GetPaletteEntries(UINT, PALETTEENTRY *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetCurrentTexturePalette(UINT) override { return D3D_OK; }
    HRESULT GetCurrentTexturePalette(UINT *) override { return D3DERR_INVALIDCALL; }
    HRESULT SetScissorRect(const RECT *) override;
    HRESULT GetScissorRect(RECT *) override;
    HRESULT SetSoftwareVertexProcessing(BOOL) override { return D3D_OK; }
    BOOL GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT SetNPatchMode(float) override { return D3D_OK; }
    float GetNPatchMode() override { return 0.0f; }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) override;
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT) override;
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *, UINT) override;
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT) override;
    HRESULT ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *, DWORD) override
    {
        return D3DERR_INVALIDCALL;
    }
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, IDirect3DVertexDeclaration9 **) override;
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *) override;
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **) override;
    HRESULT SetFVF(DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT GetFVF(DWORD *f) override
    {
        *f = 0;
        return D3D_OK;
    }
    HRESULT CreateVertexShader(const DWORD *, IDirect3DVertexShader9 **) override;
    HRESULT SetVertexShader(IDirect3DVertexShader9 *) override;
    HRESULT GetVertexShader(IDirect3DVertexShader9 **) override;
    HRESULT SetVertexShaderConstantF(UINT, const float *, UINT) override;
    HRESULT GetVertexShaderConstantF(UINT, float *, UINT) override;
    HRESULT SetVertexShaderConstantI(UINT, const int *, UINT) override;
    HRESULT GetVertexShaderConstantI(UINT, int *, UINT) override;
    HRESULT SetVertexShaderConstantB(UINT, const BOOL *, UINT) override;
    HRESULT GetVertexShaderConstantB(UINT, BOOL *, UINT) override;
    HRESULT SetStreamSource(UINT, IDirect3DVertexBuffer9 *, UINT, UINT) override;
    HRESULT GetStreamSource(UINT, IDirect3DVertexBuffer9 **, UINT *, UINT *) override;
    HRESULT SetStreamSourceFreq(UINT, UINT) override { return D3D_OK; }
    HRESULT GetStreamSourceFreq(UINT, UINT *s) override
    {
        *s = 1;
        return D3D_OK;
    }
    HRESULT SetIndices(IDirect3DIndexBuffer9 *) override;
    HRESULT GetIndices(IDirect3DIndexBuffer9 **) override;
    HRESULT CreatePixelShader(const DWORD *, IDirect3DPixelShader9 **) override;
    HRESULT SetPixelShader(IDirect3DPixelShader9 *) override;
    HRESULT GetPixelShader(IDirect3DPixelShader9 **) override;
    HRESULT SetPixelShaderConstantF(UINT, const float *, UINT) override;
    HRESULT GetPixelShaderConstantF(UINT, float *, UINT) override;
    HRESULT SetPixelShaderConstantI(UINT, const int *, UINT) override;
    HRESULT GetPixelShaderConstantI(UINT, int *, UINT) override;
    HRESULT SetPixelShaderConstantB(UINT, const BOOL *, UINT) override;
    HRESULT GetPixelShaderConstantB(UINT, BOOL *, UINT) override;
    HRESULT DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) override { return D3DERR_INVALIDCALL; }
    HRESULT DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) override { return D3DERR_INVALIDCALL; }
    HRESULT DeletePatch(UINT) override { return D3D_OK; }
    HRESULT CreateQuery(D3DQUERYTYPE, IDirect3DQuery9 **) override;

    // ---- internals shared with the resource code
    std::atomic<int> rc{1};
    bool gl = false;               // a WebGL2 context exists
    pthread_t glThread{};
    bool created = false;
    IDirect3D9 *owner = nullptr;
    D3DPRESENT_PARAMETERS pp{};

    bool onGl() const { return gl && pthread_equal(pthread_self(), glThread); }
    void queueDelete(GlKind k, GLuint id);
    void flushDeletes();

    bool create(IDirect3D9 *owner, const D3DPRESENT_PARAMETERS &pp);
    void createBackbuffer();

    // GL helpers (GL thread only)
    void bindTextureScratch(GLenum target, GLuint tex); // binds to the upload unit
    void forgetTexture(GLuint tex);
    void forgetFbosFor(KSurface *s);   // any thread
    void forgetFbosNow(KSurface *s);   // GL thread
    GLuint fboFor(KSurface *c, KSurface *d);
    void bindDrawFbo(GLuint fbo);
    void applyTarget();
    void applyViewportScissor();
    void applyRenderStates();
    bool prepareDraw(bool indexed);
    Program *getProgram(KVShader *vs, KPShader *ps, uint32_t shadowMask);
    uint32_t shadowMaskFor(const KPShader *ps) const;
    bool passIgnoresSceneDepth(const Program *p, const KSurface *d) const;
    void writebackMismatchedDepth();
    void syncMismatchedDepth(const Program *p);
    void uploadUp(GLenum target, GLuint &bo, GLsizeiptr &cap, GLsizeiptr bytes, const void *data);
    bool finishProgram(Program *pr, bool wait);
    void releaseProgramsFor(void *shader);
    void uploadConstants(Program *p);
    void bindTexturesAndSamplers(Program *p);
    GLuint samplerObject(int unit, bool compare);
    void setupAttributes(int baseVertex, const KDecl *decl, Program *p);
    void blit(KSurface *src, const RECT *sr, KSurface *dst, const RECT *dr, D3DTEXTUREFILTERTYPE filter);
    void presentBackbuffer();
    void tickShaderWarmup();
    void clearShaderWarmup();
    struct WarmTask
    {
        KVShader *vs;
        KPShader *ps;
        uint32_t shadow;
        std::string name;
        Program *program = nullptr;
        bool completed = false;
    };
    std::vector<WarmTask> warmTasks;
    size_t warmCursor = 0;
    size_t warmPollCursor = 0;
    int warmDone = 0;
    const void *warmWorld = nullptr;
    bool warmStarted = false;
    double warmReportAt = 0;

    // ---- state (CPU side)
    DWORD rs[256];
    DWORD ss[20][16];
    TexCore *tex[16] = {};
    IUnknown *texCom[16] = {};
    struct Stream
    {
        KVertexBuffer *vb = nullptr;
        UINT off = 0, stride = 0;
    } streams[4];
    KIndexBuffer *ib = nullptr;
    KDecl *decl = nullptr;
    KVShader *vs = nullptr;
    KPShader *ps = nullptr;
    KSurface *rt[4] = {};
    KSurface *ds = nullptr;
    KSurface *backBuffer = nullptr;
    D3DVIEWPORT9 vp{};
    RECT scissor{};
    D3DGAMMARAMP gamma{};

    float vsF[256 * 4];
    float psF[256 * 4];
    uint32_t vsFseq[256];
    uint32_t psFseq[256];
    uint32_t fseq = 1;
    int vsI[16 * 4];
    int psI[16 * 4];
    int vsB[16];
    int psB[16];
    uint32_t iseq = 1, bseq = 1;

    // ---- GL state cache
    std::mutex delMu;
    std::vector<std::pair<GlKind, GLuint>> dead;
    std::vector<KSurface *> deadSurf;
    std::mutex progMu;
    std::vector<FboEntry> fbos;
    std::map<ProgKey, Program *> programs;
    std::unordered_map<uint64_t, GLuint> samplers;
    GLuint boundFbo = 0xFFFFFFFFu;
    GLuint curProgram = 0;
    GLuint constantBuffers[2] = {};
    uint32_t constantFloatSeq[2] = {}, constantIntSeq[2] = {}, constantBoolSeq[2] = {};
    Program *drawProg = nullptr; // filled by prepareDraw; draw calls must not take progMu again
    GLuint curArrayBuf = 0, curElemBuf = 0;
    GLuint boundTex[34] = {};
    GLenum boundTarget[34] = {};
    GLuint boundSampler[34] = {};
    int activeUnit = -1;
    AttrState attr[16];
    GLuint dummyTex[3] = {};
    GLuint defaultVao = 0;
    struct UpSlot
    {
        GLuint vbo = 0, ibo = 0;
        GLsizeiptr vCap = 0, iCap = 0;
    };
    UpSlot upRing[4];
    int upRingPos = 0;
    bool targetDirty = true;
    bool vpDirty = true;
    std::bitset<8> groupDirty; // see RG_* in web_d3d9_device.cpp
    int targetW = 0, targetH = 0;
    bool floatLinear = false;
    int aniso = 1;
    uint64_t frame = 0;
    uint64_t drawCalls = 0, skippedDraws = 0;
    bool warnedThread = false;
    GLuint blitProgram = 0;
};

} // namespace kd

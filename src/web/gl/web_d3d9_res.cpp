// Direct3D 9 resources over WebGL2: formats, textures, surfaces, buffers, shaders (CPU half + lazy GL
// realization). See web_d3d9_priv.h.
#include "web_d3d9_device.h"
#include <web/gpu/web_gpu.h>

#include <algorithm>
#include <cstdio>

#define KLOG(...)                                   \
    do                                              \
    {                                               \
        fprintf(stderr, "d3d9gl: " __VA_ARGS__);    \
        fprintf(stderr, "\n");                      \
    } while (0)

namespace kd
{
// 16-bit normalized formats (EXT_texture_norm16) and sRGB S3TC. WebGL2's core headers omit them.
#ifndef GL_R16_EXT
#define GL_R16_EXT 0x822A
#endif
#ifndef GL_RG16_EXT
#define GL_RG16_EXT 0x822C
#endif
#ifndef GL_RGBA16_EXT
#define GL_RGBA16_EXT 0x805B
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

// ================================================================================================
// Formats
// ================================================================================================
static const FmtInfo kFmts[] = {
    // fmt, cpuBytes, compressed, depth, stencil, internal, format, type, swizzle, conversion, colorRenderable
    {D3DFMT_A8R8G8B8, 4, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_BGRA, CV_NONE, true},
    {D3DFMT_X8R8G8B8, 4, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_BGR1, CV_NONE, true},
    {D3DFMT_A8B8G8R8, 4, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, true},
    {D3DFMT_X8B8G8R8, 4, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_RGB1, CV_NONE, true},
    {D3DFMT_R5G6B5, 2, false, false, false, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, SWZ_NONE, CV_NONE, true},
    {D3DFMT_A8, 1, false, false, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE, SWZ_A8, CV_NONE, true},
    {D3DFMT_L8, 1, false, false, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE, SWZ_L8, CV_NONE, true},
    {D3DFMT_A8L8, 2, false, false, false, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, SWZ_A8L8, CV_NONE, true},
    {D3DFMT_R8G8B8, 3, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_R8G8B8, false},
    {D3DFMT_A4R4G4B4, 2, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_A4R4G4B4, false},
    {D3DFMT_X4R4G4B4, 2, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_X4R4G4B4, false},
    {D3DFMT_A1R5G5B5, 2, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_A1R5G5B5, false},
    {D3DFMT_X1R5G5B5, 2, false, false, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_X1R5G5B5, false},
    {D3DFMT_V8U8, 2, false, false, false, GL_RG8_SNORM, GL_RG, GL_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_Q8W8V8U8, 4, false, false, false, GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_A2B10G10R10, 4, false, false, false, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, SWZ_NONE, CV_NONE, true},
    {D3DFMT_A2R10G10B10, 4, false, false, false, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, SWZ_BGRA, CV_NONE, true},
    {D3DFMT_G16R16, 4, false, false, false, GL_RG16_EXT, GL_RG, GL_UNSIGNED_SHORT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_L16, 2, false, false, false, GL_R16_EXT, GL_RED, GL_UNSIGNED_SHORT, SWZ_L8, CV_NONE, false},
    // A16B16G16R16 is a D3D9 render target; G16R16 and L16 are not.
    {D3DFMT_A16B16G16R16, 8, false, false, false, GL_RGBA16_EXT, GL_RGBA, GL_UNSIGNED_SHORT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_R16F, 2, false, false, false, GL_R16F, GL_RED, GL_HALF_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_G16R16F, 4, false, false, false, GL_RG16F, GL_RG, GL_HALF_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_A16B16G16R16F, 8, false, false, false, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_R32F, 4, false, false, false, GL_R32F, GL_RED, GL_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_G32R32F, 8, false, false, false, GL_RG32F, GL_RG, GL_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_A32B32G32R32F, 16, false, false, false, GL_RGBA32F, GL_RGBA, GL_FLOAT, SWZ_NONE, CV_NONE, true},
    {D3DFMT_DXT1, 8, true, false, false, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_DXT2, 16, true, false, false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_DXT3, 16, true, false, false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_DXT4, 16, true, false, false, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_DXT5, 16, true, false, false, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D16, 2, false, true, false, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D16_LOCKABLE, 2, false, true, false, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D24X8, 4, false, true, false, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D24S8, 4, false, true, true, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D32F_LOCKABLE, 4, false, true, false, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D32, 4, false, true, false, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, SWZ_NONE, CV_NONE, false},
    {D3DFMT_D24FS8, 8, false, true, true, GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, SWZ_NONE, CV_NONE, false},
    {FMT_INTZ, 4, false, true, true, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, SWZ_NONE, CV_NONE, false},
};

const FmtInfo *FindFmt(D3DFORMAT f)
{
    for (const FmtInfo &i : kFmts)
        if (i.fmt == f)
            return &i;
    return nullptr;
}

size_t LevelPitch(const FmtInfo &fi, UINT w)
{
    if (fi.compressed)
        return (size_t)std::max<UINT>(1, (w + 3) / 4) * fi.cpuBytes;
    return (size_t)w * fi.cpuBytes;
}

size_t LevelRows(const FmtInfo &fi, UINT h)
{
    return fi.compressed ? std::max<UINT>(1, (h + 3) / 4) : h;
}

static void ConvertToGl(Conv c, const uint8_t *src, UINT w, UINT h, size_t srcPitch, std::vector<uint8_t> &dst)
{
    auto u16 = [&](const uint8_t *p) { return (unsigned)(p[0] | (p[1] << 8)); };
    switch (c)
    {
    case CV_R8G8B8:
        dst.resize((size_t)w * h * 4);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                const uint8_t *s = src + y * srcPitch + x * 3;
                uint8_t *d = &dst[((size_t)y * w + x) * 4];
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
            }
        break;
    case CV_A4R4G4B4: case CV_X4R4G4B4:
        dst.resize((size_t)w * h * 4);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                const unsigned v = u16(src + y * srcPitch + x * 2);
                uint8_t *d = &dst[((size_t)y * w + x) * 4];
                d[0] = (uint8_t)(((v >> 8) & 15) * 17);
                d[1] = (uint8_t)(((v >> 4) & 15) * 17);
                d[2] = (uint8_t)((v & 15) * 17);
                d[3] = c == CV_A4R4G4B4 ? (uint8_t)(((v >> 12) & 15) * 17) : 255;
            }
        break;
    case CV_A1R5G5B5: case CV_X1R5G5B5:
        dst.resize((size_t)w * h * 4);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                const unsigned v = u16(src + y * srcPitch + x * 2);
                uint8_t *d = &dst[((size_t)y * w + x) * 4];
                d[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
                d[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
                d[2] = (uint8_t)((v & 31) * 255 / 31);
                d[3] = c == CV_A1R5G5B5 ? ((v & 0x8000) ? 255 : 0) : 255;
            }
        break;
    default: break;
    }
}

// ================================================================================================
// Textures
// ================================================================================================
size_t TexCore::levelBytes(UINT l) const
{
    return LevelPitch(*fi, levelW(l)) * LevelRows(*fi, levelH(l)) * (type == 2 ? levelD(l) : 1);
}

bool TexCore::init(Device *dv, int t, UINT width, UINT height, UINT depth, UINT lvls, DWORD use, D3DFORMAT f, D3DPOOL p)
{
    dev = dv;
    type = t;
    fmt = f;
    fi = FindFmt(f);
    if (!fi)
    {
        KLOG("unsupported texture format 0x%x", (unsigned)f);
        return false;
    }
    usage = use;
    pool = p;
    w = width;
    h = height;
    d = depth ? depth : 1;
    const UINT maxDim = std::max(w, std::max(h, d));
    UINT full = 1;
    while ((maxDim >> full) != 0)
        ++full;
    levels = lvls ? std::min(lvls, full) : full;
    lv.resize((size_t)faces() * levels);
    surfaces.assign((size_t)faces() * levels, nullptr);
    return true;
}

HRESULT TexCore::lock(UINT face, UINT level, const RECT *r, const D3DBOX *b, void **bits, INT *pitch, INT *slice, DWORD flags)
{
    if (level >= levels || face >= (UINT)faces())
        return D3DERR_INVALIDCALL;
    if (isRT() || isDS())
        return D3DERR_INVALIDCALL;
    std::lock_guard<std::mutex> g(mu);
    Level &L = lv[(size_t)face * levels + level];
    const size_t bytes = levelBytes(level);
    if (L.data.size() != bytes)
        L.data.assign(bytes, 0);
    const size_t rowPitch = LevelPitch(*fi, levelW(level));
    size_t off = 0;
    if (r)
    {
        if (fi->compressed)
            off = (size_t)(r->top / 4) * rowPitch + (size_t)(r->left / 4) * fi->cpuBytes;
        else
            off = (size_t)r->top * rowPitch + (size_t)r->left * fi->cpuBytes;
    }
    else if (b)
    {
        const size_t slicePitch = rowPitch * LevelRows(*fi, levelH(level));
        off = (size_t)b->Front * slicePitch + (size_t)b->Top * rowPitch + (size_t)b->Left * fi->cpuBytes;
    }
    *bits = L.data.data() + off;
    *pitch = (INT)rowPitch;
    if (slice)
        *slice = (INT)(rowPitch * LevelRows(*fi, levelH(level)));
    if (!(flags & D3DLOCK_READONLY))
    {
        L.dirty = true;
        anyDirty = true;
        mipDirty = true;
    }
    return D3D_OK;
}

HRESULT TexCore::unlock(UINT face, UINT level, DWORD)
{
    if (level >= levels || face >= (UINT)faces())
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

// D3D A8R8G8B8 / X8R8G8B8 are B,G,R in memory. X8B8G8R8 is already R,G,B, but the unused
// byte is often 0, and an alpha test then throws the texel away. WebGL2 has no texture
// swizzle, so fix both here. L8 / A8 / A8L8 stay R/RG; the pixel shader replicates them.
// Render targets are never uploaded, so their storage format is left alone.
static const uint8_t *SwizzleUpload(const FmtInfo &fi, const uint8_t *src, UINT w, UINT h, UINT d, std::vector<uint8_t> &tmp)
{
    if (!src || fi.type != GL_UNSIGNED_BYTE || fi.format != GL_RGBA)
        return src;
    const bool swapRb = fi.swz == SWZ_BGRA || fi.swz == SWZ_BGR1;
    const bool forceA = fi.swz == SWZ_BGR1 || fi.swz == SWZ_RGB1;
    if (!swapRb && !forceA)
        return src;
    if (d == 0)
        d = 1;
    const size_t pitch = (size_t)w * 4;
    const size_t bytes = pitch * (size_t)h * (size_t)d;
    if (tmp.size() < bytes)
        tmp.resize(bytes);
    for (UINT z = 0; z < d; ++z)
        for (UINT y = 0; y < h; ++y)
        {
            const uint8_t *row = src + ((size_t)z * h + y) * pitch;
            uint8_t *dst = tmp.data() + ((size_t)z * h + y) * pitch;
            for (UINT x = 0; x < w; ++x)
            {
                const uint8_t c0 = row[x * 4 + 0];
                const uint8_t c1 = row[x * 4 + 1];
                const uint8_t c2 = row[x * 4 + 2];
                const uint8_t c3 = row[x * 4 + 3];
                dst[x * 4 + 0] = swapRb ? c2 : c0;
                dst[x * 4 + 1] = c1;
                dst[x * 4 + 2] = swapRb ? c0 : c2;
                dst[x * 4 + 3] = forceA ? 255 : c3;
            }
        }
    return tmp.data();
}

// glGenerateMipmap is legal for these sized formats. S3TC and the float/snorm formats are not:
// WebGL rejects the call, and a clamped base level is the fix there.
static bool CanGenerateMips(const FmtInfo &fi)
{
    if (fi.compressed || fi.depth || fi.stencil)
        return false;
    switch (fi.internalFormat)
    {
    case GL_R8:
    case GL_RG8:
    case GL_RGB565:
    case GL_RGBA8:
    case GL_RGB10_A2:
    case GL_R16_EXT:
    case GL_RG16_EXT:
    case GL_RGBA16_EXT:
    case GL_R16F:
    case GL_RG16F:
    case GL_RGBA16F:
        return true;
    default:
        return false;
    }
}

static void UploadedRun(uint32_t authored, UINT levels, int &first, int &last)
{
    first = -1;
    last = -1;
    const UINT n = levels < 32 ? levels : 32;
    for (UINT l = 0; l < n; ++l)
    {
        if ((authored & (1u << l)) == 0)
        {
            if (first >= 0)
                break;
            continue;
        }
        if (first < 0)
            first = (int)l;
        last = (int)l;
    }
}

// BASE/MAX describe one contiguous defined run. A mipmapped sampler then clamps instead of
// blending in the zero levels glTexStorage left behind. SetLOD raises the base further.
// One level is enough for the texture to stay complete.
// Mips are generated only when D3D asked for it. A box filter of a normal map is not a normal,
// and rebuilding the chain when a later mip arrives makes the previous frame sample black.
static void ApplyMipRange(TexCore &t, bool uploadedBase)
{
    if (!t.tex)
        return;
    int first = -1, last = -1;
    UploadedRun(t.authored, t.levels, first, last);

    bool rebasing = false;
    const bool wantGen = t.autoGen() && t.levels > 1 && t.fi && CanGenerateMips(*t.fi) && (uploadedBase || t.mipDirty);
    if (wantGen)
    {
        t.dev->bindTextureScratch(t.target(), t.tex);
        // Generation reads BASE_LEVEL and, on some drivers, stops at MAX_LEVEL. Open the
        // whole chain first; the lod clamp below writes the final range.
        glTexParameteri(t.target(), GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(t.target(), GL_TEXTURE_MAX_LEVEL, (GLint)t.levels - 1);
        while (glGetError() != GL_NO_ERROR)
            ;
        glGenerateMipmap(t.target());
        bool genOk = true;
        for (;;)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR)
                break;
            genOk = false;
        }
        // A rejected generate must not leave MAX_LEVEL on the empty tail. Fall through and clamp.
        t.mipsGenerated = genOk;
        rebasing = true;
    }
    else if (!t.autoGen())
        t.mipsGenerated = false;
    t.mipDirty = false;

    int base = 0;
    int maxL = 0;
    if (t.mipsGenerated)
    {
        base = 0;
        maxL = (int)t.levels - 1;
    }
    else if (first >= 0)
    {
        base = first;
        maxL = last;
    }
    if (maxL < 0)
        maxL = 0;
    if (t.lod > (DWORD)base)
        base = (int)std::min<DWORD>(t.lod, (DWORD)maxL);
    if (base > maxL)
        base = maxL;
    if (base < 0)
        base = 0;
    if (!rebasing && base == t.appliedBase && maxL == t.appliedMax)
        return;
    t.dev->bindTextureScratch(t.target(), t.tex);
    glTexParameteri(t.target(), GL_TEXTURE_BASE_LEVEL, base);
    glTexParameteri(t.target(), GL_TEXTURE_MAX_LEVEL, maxL);
    t.appliedBase = base;
    t.appliedMax = maxL;
}

static bool HaveNorm16()
{
    static int v = -1;
    if (v < 0)
        v = GlHasExt("EXT_texture_norm16") ? 1 : 0;
    return v != 0;
}

static bool HaveS3tcSrgb()
{
    static int v = -1;
    if (v < 0)
        v = GlHasExt("WEBGL_compressed_texture_s3tc_srgb") ? 1 : 0;
    return v != 0;
}

// 0 when this D3D format has no WebGL sRGB storage (or the sRGB S3TC extension is missing).
static GLenum SrgbStorage(const FmtInfo &fi)
{
    switch (fi.fmt)
    {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_R8G8B8:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
        return GL_SRGB8_ALPHA8;
    case D3DFMT_DXT1:
        return HaveS3tcSrgb() ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : 0;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
        return HaveS3tcSrgb() ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT : 0;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return HaveS3tcSrgb() ? (GLenum)GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : 0;
    default:
        return 0;
    }
}

static bool IsNorm16(GLenum f)
{
    return f == GL_R16_EXT || f == GL_RG16_EXT || f == GL_RGBA16_EXT;
}

static GLenum Norm16AsFloat(GLenum f)
{
    if (f == GL_R16_EXT)
        return GL_R16F;
    if (f == GL_RG16_EXT)
        return GL_RG16F;
    if (f == GL_RGBA16_EXT)
        return GL_RGBA16F;
    return f;
}

static uint16_t FloatToHalf(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    const uint32_t sign = (u >> 16) & 0x8000u;
    const uint32_t exp0 = (u >> 23) & 0xFFu;
    uint32_t mant = u & 0x7FFFFFu;
    if (exp0 == 0xFFu)
        return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0));
    int exp = (int)exp0 - 127 + 15;
    if (exp >= 31)
        return (uint16_t)(sign | 0x7C00u);
    if (exp <= 0)
    {
        if (exp < -10)
            return (uint16_t)sign;
        mant |= 0x800000u;
        const uint32_t shift = (uint32_t)(1 - exp);
        uint32_t half = mant >> (shift + 13);
        if ((mant >> (shift + 12)) & 1u)
            ++half;
        return (uint16_t)(sign | half);
    }
    uint16_t half = (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
    if (mant & 0x1000u)
        half = (uint16_t)(half + 1);
    return half;
}

static void Unorm16ToHalf(const uint8_t *src, size_t count, std::vector<uint8_t> &dst)
{
    dst.resize(count * 2);
    auto *out = reinterpret_cast<uint16_t *>(dst.data());
    for (size_t i = 0; i < count; ++i)
    {
        uint16_t u;
        memcpy(&u, src + i * 2, 2);
        out[i] = FloatToHalf(u / 65535.0f);
    }
}

// hardwareSrgb selects the sRGB sized/compressed format. asHalf is set when a 16-bit unorm
// format had to fall back to *16F. upType is the TexSubImage type that matches ifmt.
static GLenum ResolveInternal(const FmtInfo &fi, bool hardwareSrgb, bool &asHalf, GLenum &upType)
{
    GLenum ifmt = fi.internalFormat;
    if (hardwareSrgb)
    {
        const GLenum s = SrgbStorage(fi);
        if (s)
            ifmt = s;
    }
    asHalf = false;
    upType = fi.type;
    if (IsNorm16(ifmt) && !HaveNorm16())
    {
        ifmt = Norm16AsFloat(ifmt);
        upType = GL_HALF_FLOAT;
        asHalf = true;
    }
    return ifmt;
}

void TexCore::realize()
{
    Device &D = *dev;
    if (!D.onGl())
        return;
    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_SCRATCH)
        return;
    if (!tex)
    {
        glGenTextures(1, &tex);
        D.bindTextureScratch(target(), tex);
        bool asHalf = false;
        GLenum upType = 0;
        const GLenum ifmt = ResolveInternal(*fi, srgbMode == 2, asHalf, upType);
        storageInternal = ifmt;
        uploadType = upType;
        uploadHalf = asHalf;
        appliedAniso = 0;
        if (type == 2)
            glTexStorage3D(GL_TEXTURE_3D, (GLsizei)levels, ifmt, (GLsizei)w, (GLsizei)h, (GLsizei)d);
        else
            glTexStorage2D(target(), (GLsizei)levels, ifmt, (GLsizei)w, (GLsizei)h);
        anyDirty = true;
        authored = 0;
        for (uint8_t &got : faceGot)
            got = 0;
        mipsGenerated = false;
        appliedBase = -1;
        appliedMax = -1;
    }
    bool uploadedBase = false;
    if (anyDirty)
    {
        std::lock_guard<std::mutex> g(mu);
        D.bindTextureScratch(target(), tex);
        std::vector<uint8_t> tmp, halfBuf;
        const GLenum storeFmt = storageInternal ? storageInternal : fi->internalFormat;
        const GLenum storeType = uploadType ? uploadType : fi->type;
        for (int face = 0; face < faces(); ++face)
            for (UINT l = 0; l < levels; ++l)
            {
                Level &L = lv[(size_t)face * levels + l];
                if (!L.dirty || L.data.empty())
                    continue;
                const UINT lw = levelW(l), lh = levelH(l), ld = levelD(l);
                const GLenum tgt = type == 1 ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : target();
                const uint8_t *ptr = L.data.data();
                if (fi->compressed)
                {
                    glCompressedTexSubImage2D(tgt, (GLint)l, 0, 0, (GLsizei)lw, (GLsizei)lh, storeFmt,
                                              (GLsizei)L.data.size(), ptr);
                }
                else
                {
                    if (fi->conv != CV_NONE)
                    {
                        ConvertToGl(fi->conv, ptr, lw, lh, LevelPitch(*fi, lw), tmp);
                        ptr = tmp.data();
                    }
                    ptr = SwizzleUpload(*fi, ptr, lw, lh, fi->conv == CV_NONE ? ld : 1, tmp);
                    if (uploadHalf)
                    {
                        const size_t comps = fi->cpuBytes / 2;
                        const size_t texels = (size_t)lw * lh * (type == 2 ? ld : 1) * (comps ? comps : 1);
                        Unorm16ToHalf(ptr, texels, halfBuf);
                        ptr = halfBuf.data();
                    }
                    if (type == 2)
                        glTexSubImage3D(GL_TEXTURE_3D, (GLint)l, 0, 0, 0, (GLsizei)lw, (GLsizei)lh, (GLsizei)ld, fi->format,
                                        storeType, ptr);
                    else
                        glTexSubImage2D(tgt, (GLint)l, 0, 0, (GLsizei)lw, (GLsizei)lh, fi->format, storeType, ptr);
                }
                L.dirty = false;
                if (l < 32)
                {
                    faceGot[l] |= (uint8_t)(1u << face);
                    const uint8_t need = (uint8_t)((1u << faces()) - 1u);
                    if ((faceGot[l] & need) == need)
                    {
                        authored |= 1u << l;
                        if (l == 0)
                            uploadedBase = true;
                    }
                }
                // The model-lighting volume is patched a few entries at a time
                // even though D3DUSAGE_DYNAMIC is not set. LockBox must retain
                // the untouched cells; reallocating a zero buffer flashes props black.
                if (!(usage & D3DUSAGE_DYNAMIC) && type != 2)
                {
                    L.data.clear();
                    L.data.shrink_to_fit();
                }
            }
        anyDirty = false;
    }
    ApplyMipRange(*this, uploadedBase);
}

static bool BaseLevelResident(TexCore &t)
{
    std::lock_guard<std::mutex> g(t.mu);
    if (t.lv.empty() || t.levels == 0)
        return false;
    for (int f = 0; f < t.faces(); ++f)
    {
        const Level &L = t.lv[(size_t)f * t.levels];
        if (L.data.size() != t.levelBytes(0))
            return false;
    }
    return true;
}

static void DropTextureForRebuild(TexCore &t)
{
    if (!t.tex || !t.dev)
        return;
    t.dev->forgetTexture(t.tex);
    glDeleteTextures(1, &t.tex);
    t.tex = 0;
    t.storageInternal = 0;
    t.uploadType = 0;
    t.uploadHalf = false;
    t.appliedAniso = 0;
    t.appliedBase = -1;
    t.appliedMax = -1;
    t.mipsGenerated = false;
    t.authored = 0;
    for (uint8_t &got : t.faceGot)
        got = 0;
    std::lock_guard<std::mutex> g(t.mu);
    for (Level &L : t.lv)
    {
        if (L.data.empty())
            continue;
        L.dirty = true;
        t.anyDirty = true;
    }
}

int TexCore::commitSrgb(bool want)
{
    if (!fi || fi->depth || isRT() || isDS())
        return want ? 1 : 0;
    const GLenum srgb = SrgbStorage(*fi);
    if (!srgb)
        return want ? 1 : 0;
    if (!tex)
    {
        // First sample picks the storage format. Later samplers in the same draw
        // must not flip it, or their shader op would disagree with the upload.
        if (srgbMode == 0)
            srgbMode = want ? 2 : 1;
        if (srgbMode == 2)
            return want ? 0 : 2;
        return want ? 1 : 0;
    }
    const bool storedSrgb = storageInternal == srgb;
    if (storedSrgb)
    {
        if (want)
            return 0;
        if (BaseLevelResident(*this))
        {
            DropTextureForRebuild(*this);
            srgbMode = 3;
            return 0;
        }
        srgbMode = 3;
        return 2;
    }
    if (!want)
        return 0;
    if (BaseLevelResident(*this))
    {
        DropTextureForRebuild(*this);
        srgbMode = 2;
        return 0;
    }
    srgbMode = 3;
    return 1;
}

void TexCore::release()
{
    if (gpuId) { webgpu::Release(gpuId); gpuId = 0; }
    if (tex && dev)
    {
        dev->queueDelete(GLK_TEXTURE, tex);
        tex = 0;
    }
}

TexCore::~TexCore()
{
    release();
    for (KSurface *s : surfaces)
    {
        if (s && dev)
            dev->forgetFbosFor(s);
        delete s;
    }
}

// ---- surfaces of texture levels
static KSurface *LevelSurface(TexCore *c, IUnknown *com, UINT face, UINT level)
{
    KSurface *&s = c->surfaces[(size_t)face * c->levels + level];
    if (!s)
    {
        s = new KSurface();
        s->dev = c->dev;
        s->kind = SK_TEXLEVEL;
        s->fmt = c->fmt;
        s->fi = c->fi;
        s->w = c->levelW(level);
        s->h = c->levelH(level);
        s->usage = c->usage;
        s->pool = c->pool;
        s->parent = c;
        s->parentCom = com;
        s->face = face;
        s->level = level;
    }
    return s;
}

#define RESOURCE_BODY(TYPE)                                                                    \
    HRESULT GetDevice(IDirect3DDevice9 **pp) override                                          \
    {                                                                                          \
        *pp = c.dev;                                                                           \
        c.dev->AddRef();                                                                       \
        return D3D_OK;                                                                         \
    }                                                                                          \
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }    \
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_NOTFOUND; }      \
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }                               \
    DWORD SetPriority(DWORD) override { return 0; }                                            \
    DWORD GetPriority() override { return 0; }                                                 \
    void PreLoad() override {}                                                                 \
    D3DRESOURCETYPE GetType() override { return TYPE; }                                        \
    DWORD SetLOD(DWORD n) override                                                             \
    {                                                                                          \
        const DWORD old = c.lod;                                                               \
        c.lod = n;                                                                             \
        return old;                                                                            \
    }                                                                                          \
    DWORD GetLOD() override { return c.lod; }                                                  \
    DWORD GetLevelCount() override { return c.levels; }                                        \
    HRESULT SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) override                              \
    {                                                                                          \
        c.autoFilter = f;                                                                      \
        return D3D_OK;                                                                         \
    }                                                                                          \
    D3DTEXTUREFILTERTYPE GetAutoGenFilterType() override { return c.autoFilter; }              \
    void GenerateMipSubLevels() override { c.mipDirty = true; }

struct KTexture : Unk<IDirect3DTexture9>
{
    TexCore c;
    RESOURCE_BODY(D3DRTYPE_TEXTURE)
    HRESULT GetLevelDesc(UINT l, D3DSURFACE_DESC *d) override
    {
        if (l >= c.levels)
            return D3DERR_INVALIDCALL;
        d->Format = c.fmt;
        d->Type = D3DRTYPE_SURFACE;
        d->Usage = c.usage;
        d->Pool = c.pool;
        d->MultiSampleType = D3DMULTISAMPLE_NONE;
        d->MultiSampleQuality = 0;
        d->Width = c.levelW(l);
        d->Height = c.levelH(l);
        return D3D_OK;
    }
    HRESULT GetSurfaceLevel(UINT l, IDirect3DSurface9 **pp) override
    {
        if (l >= c.levels)
            return D3DERR_INVALIDCALL;
        KSurface *s = LevelSurface(&c, this, 0, l);
        s->AddRef();
        *pp = s;
        return D3D_OK;
    }
    HRESULT LockRect(UINT l, D3DLOCKED_RECT *lr, const RECT *r, DWORD f) override
    {
        return c.lock(0, l, r, nullptr, &lr->pBits, &lr->Pitch, nullptr, f);
    }
    HRESULT UnlockRect(UINT l) override { return c.unlock(0, l, 0); }
    HRESULT AddDirtyRect(const RECT *) override { return D3D_OK; }
};

struct KCubeTexture : Unk<IDirect3DCubeTexture9>
{
    TexCore c;
    RESOURCE_BODY(D3DRTYPE_CUBETEXTURE)
    HRESULT GetLevelDesc(UINT l, D3DSURFACE_DESC *d) override
    {
        if (l >= c.levels)
            return D3DERR_INVALIDCALL;
        d->Format = c.fmt;
        d->Type = D3DRTYPE_SURFACE;
        d->Usage = c.usage;
        d->Pool = c.pool;
        d->MultiSampleType = D3DMULTISAMPLE_NONE;
        d->MultiSampleQuality = 0;
        d->Width = c.levelW(l);
        d->Height = c.levelH(l);
        return D3D_OK;
    }
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT l, IDirect3DSurface9 **pp) override
    {
        if (l >= c.levels || (UINT)face >= 6)
            return D3DERR_INVALIDCALL;
        KSurface *s = LevelSurface(&c, this, (UINT)face, l);
        s->AddRef();
        *pp = s;
        return D3D_OK;
    }
    HRESULT LockRect(D3DCUBEMAP_FACES face, UINT l, D3DLOCKED_RECT *lr, const RECT *r, DWORD f) override
    {
        return c.lock((UINT)face, l, r, nullptr, &lr->pBits, &lr->Pitch, nullptr, f);
    }
    HRESULT UnlockRect(D3DCUBEMAP_FACES face, UINT l) override { return c.unlock((UINT)face, l, 0); }
    HRESULT AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) override { return D3D_OK; }
};

struct KVolumeTexture : Unk<IDirect3DVolumeTexture9>
{
    TexCore c;
    RESOURCE_BODY(D3DRTYPE_VOLUMETEXTURE)
    HRESULT GetLevelDesc(UINT l, D3DVOLUME_DESC *d) override
    {
        if (l >= c.levels)
            return D3DERR_INVALIDCALL;
        d->Format = c.fmt;
        d->Type = D3DRTYPE_VOLUME;
        d->Usage = c.usage;
        d->Pool = c.pool;
        d->Width = c.levelW(l);
        d->Height = c.levelH(l);
        d->Depth = c.levelD(l);
        return D3D_OK;
    }
    HRESULT GetVolumeLevel(UINT, IDirect3DVolume9 **) override { return D3DERR_INVALIDCALL; }
    HRESULT LockBox(UINT l, D3DLOCKED_BOX *lb, const D3DBOX *b, DWORD f) override
    {
        return c.lock(0, l, nullptr, b, &lb->pBits, &lb->RowPitch, &lb->SlicePitch, f);
    }
    HRESULT UnlockBox(UINT l) override { return c.unlock(0, l, 0); }
    HRESULT AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
};

// Used by the device.
TexCore *TexCoreOf(IDirect3DBaseTexture9 *t)
{
    if (!t)
        return nullptr;
    switch (t->GetType())
    {
    case D3DRTYPE_TEXTURE: return &static_cast<KTexture *>(t)->c;
    case D3DRTYPE_CUBETEXTURE: return &static_cast<KCubeTexture *>(t)->c;
    case D3DRTYPE_VOLUMETEXTURE: return &static_cast<KVolumeTexture *>(t)->c;
    default: return nullptr;
    }
}

HRESULT Device::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9 **pp,
                              HANDLE *)
{
    auto *t = new KTexture();
    if (!t->c.init(this, 0, w, h, 1, levels, usage, fmt, pool))
    {
        delete t;
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9 **pp,
                                  HANDLE *)
{
    auto *t = new KCubeTexture();
    if (!t->c.init(this, 1, edge, edge, 1, levels, usage, fmt, pool))
    {
        delete t;
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                    IDirect3DVolumeTexture9 **pp, HANDLE *)
{
    auto *t = new KVolumeTexture();
    if (!t->c.init(this, 2, w, h, d, levels, usage, fmt, pool))
    {
        delete t;
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

// ================================================================================================
// Surfaces
// ================================================================================================
ULONG KSurface::AddRef()
{
    if (kind == SK_TEXLEVEL)
        return parentCom->AddRef();
    return (ULONG)++rc;
}

ULONG KSurface::Release()
{
    if (kind == SK_TEXLEVEL)
        return parentCom->Release();
    const int r = --rc;
    if (r == 0)
        OnZero();
    return (ULONG)r;
}

void KSurface::OnZero()
{
    if (dev)
        dev->forgetFbosFor(this);
    releaseGl();
    delete this;
}

void KSurface::releaseGl()
{
    if (gpuId) { webgpu::Release(gpuId); gpuId = 0; }
    if (rb && dev)
    {
        dev->queueDelete(GLK_RENDERBUFFER, rb);
        rb = 0;
    }
}

KSurface::~KSurface() {}

GLuint KSurface::glTexture() const
{
    return parent ? parent->tex : 0;
}

void KSurface::realizeRb()
{
    if (rb || kind == SK_TEXLEVEL || kind == SK_PLAIN || !dev || !dev->onGl())
        return;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    bool asHalf = false;
    GLenum upType = 0;
    const GLenum ifmt = fi ? ResolveInternal(*fi, false, asHalf, upType) : (GLenum)GL_RGBA8;
    if (ms > D3DMULTISAMPLE_NONMASKABLE)
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)ms, ifmt, (GLsizei)w, (GLsizei)h);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, ifmt, (GLsizei)w, (GLsizei)h);
}

HRESULT KSurface::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = dev;
    dev->AddRef();
    return D3D_OK;
}

HRESULT KSurface::GetContainer(REFIID, void **pp)
{
    if (parentCom)
    {
        parentCom->AddRef();
        *pp = parentCom;
        return D3D_OK;
    }
    *pp = nullptr;
    return E_NOINTERFACE;
}

HRESULT KSurface::GetDesc(D3DSURFACE_DESC *d)
{
    d->Format = fmt;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = usage;
    d->Pool = pool;
    d->MultiSampleType = ms;
    d->MultiSampleQuality = 0;
    d->Width = w;
    d->Height = h;
    return D3D_OK;
}

HRESULT KSurface::LockRect(D3DLOCKED_RECT *lr, const RECT *r, DWORD flags)
{
    if (kind == SK_TEXLEVEL)
        return parent->lock(face, level, r, nullptr, &lr->pBits, &lr->Pitch, nullptr, flags);
    if (kind != SK_PLAIN || !fi)
        return D3DERR_INVALIDCALL;
    const size_t pitch = LevelPitch(*fi, w);
    const size_t bytes = pitch * LevelRows(*fi, h);
    if (sys.size() != bytes)
        sys.assign(bytes, 0);
    size_t off = 0;
    if (r)
        off = fi->compressed ? (size_t)(r->top / 4) * pitch + (size_t)(r->left / 4) * fi->cpuBytes
                             : (size_t)r->top * pitch + (size_t)r->left * fi->cpuBytes;
    lr->pBits = sys.data() + off;
    lr->Pitch = (INT)pitch;
    locked = true;
    return D3D_OK;
}

HRESULT KSurface::UnlockRect()
{
    if (kind == SK_TEXLEVEL)
        return parent->unlock(face, level, 0);
    locked = false;
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD, BOOL, IDirect3DSurface9 **pp,
                                   HANDLE *)
{
    const FmtInfo *fi = FindFmt(fmt);
    if (!fi)
        return D3DERR_INVALIDCALL;
    auto *s = new KSurface();
    s->dev = this;
    s->kind = SK_RENDERTARGET;
    s->fmt = fmt;
    s->fi = fi;
    s->w = w;
    s->h = h;
    s->usage = D3DUSAGE_RENDERTARGET;
    s->ms = ms;
    *pp = s;
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD, BOOL, IDirect3DSurface9 **pp,
                                          HANDLE *)
{
    const FmtInfo *fi = FindFmt(fmt);
    if (!fi || !fi->depth)
        return D3DERR_INVALIDCALL;
    auto *s = new KSurface();
    s->dev = this;
    s->kind = SK_DEPTH;
    s->fmt = fmt;
    s->fi = fi;
    s->w = w;
    s->h = h;
    s->usage = D3DUSAGE_DEPTHSTENCIL;
    s->ms = ms;
    *pp = s;
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9 **pp, HANDLE *)
{
    const FmtInfo *fi = FindFmt(fmt);
    if (!fi)
        return D3DERR_INVALIDCALL;
    auto *s = new KSurface();
    s->dev = this;
    s->kind = SK_PLAIN;
    s->fmt = fmt;
    s->fi = fi;
    s->w = w;
    s->h = h;
    s->pool = pool;
    *pp = s;
    return D3D_OK;
}

// ================================================================================================
// Buffers
// ================================================================================================
bool BufferCore::init(Device *d, UINT sz, DWORD use, D3DPOOL p, GLenum tgt)
{
    dev = d;
    size = sz;
    usage = use;
    pool = p;
    target = tgt;
    // Dynamic buffers are filled from an engine pointer (skinned cache, mesh
    // verts) and uploaded once. A full-size shadow is allocated only if lock()
    // needs a place to write.
    if ((use & D3DUSAGE_DYNAMIC) == 0)
        data.assign(sz, 0);
    return true;
}

HRESULT BufferCore::lock(UINT off, UINT sz, void **pp, DWORD flags)
{
    std::lock_guard<std::mutex> g(mu);
    if ((flags & D3DLOCK_DISCARD) && (usage & D3DUSAGE_DYNAMIC)) {
        if (gpuId) webgpu::Release(gpuId);
        gpuId = 0;
        ++gpuGeneration;
        dirtyLo = 0xFFFFFFFFu; dirtyHi = 0;
    }
    external = nullptr;
    externalBytes = 0;
    if (off > size)
        return D3DERR_INVALIDCALL;
    if (sz == 0 || off + sz > size)
        sz = size - off;
    if (data.size() != size)
        data.assign(size, 0);
    *pp = data.data() + off;
    if (!(flags & D3DLOCK_READONLY))
    {
        dirtyLo = std::min(dirtyLo, off);
        dirtyHi = std::max(dirtyHi, off + sz);
    }
    locked = true;
    return D3D_OK;
}

HRESULT BufferCore::unlock()
{
    locked = false;
    return D3D_OK;
}

void BufferCore::stageExternal(const void *src, UINT off, UINT bytes)
{
    std::lock_guard<std::mutex> g(mu);
    external = nullptr;
    externalOff = 0;
    externalBytes = 0;
    dirtyLo = 0xFFFFFFFFu;
    dirtyHi = 0;
    if (!src || !bytes || off >= size)
        return;
    if (off + bytes > size)
        bytes = size - off;
    external = static_cast<const uint8_t *>(src);
    externalOff = off;
    externalBytes = bytes;
}

GLuint BufferCore::realize()
{
    if (!dev->onGl())
        return 0;
    std::lock_guard<std::mutex> g(mu);
    const bool dyn = (usage & D3DUSAGE_DYNAMIC) != 0;
    if (!bo)
    {
        glGenBuffers(1, &bo);
        glBindBuffer(target, bo);
        glBufferData(target, (GLsizeiptr)size, nullptr, dyn ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        if (target == GL_ARRAY_BUFFER)
            dev->curArrayBuf = bo;
        else
            dev->curElemBuf = bo;
        bufferSize = size;
    }
    if (external && externalBytes)
    {
        GLuint &cur = target == GL_ARRAY_BUFFER ? dev->curArrayBuf : dev->curElemBuf;
        if (cur != bo)
        {
            glBindBuffer(target, bo);
            cur = bo;
        }
        glBufferSubData(target, (GLintptr)externalOff, (GLsizeiptr)externalBytes, external);
        external = nullptr;
        externalOff = 0;
        externalBytes = 0;
    }
    else if (dirtyHi > dirtyLo && !data.empty())
    {
        GLuint &cur = target == GL_ARRAY_BUFFER ? dev->curArrayBuf : dev->curElemBuf;
        if (cur != bo)
        {
            glBindBuffer(target, bo);
            cur = bo;
        }
        glBufferSubData(target, (GLintptr)dirtyLo, (GLsizeiptr)(dirtyHi - dirtyLo), data.data() + dirtyLo);
        dirtyLo = 0xFFFFFFFFu;
        dirtyHi = 0;
        if (!dyn && !locked)
        {
            data.clear();
            data.shrink_to_fit();
        }
    }
    return bo;
}

void BufferCore::release()
{
    if (gpuId) { webgpu::Release(gpuId); gpuId = 0; }
    if (bo && dev)
    {
        if (dev->curArrayBuf == bo)
            dev->curArrayBuf = 0;
        if (dev->curElemBuf == bo)
            dev->curElemBuf = 0;
        dev->queueDelete(GLK_BUFFER, bo);
        bo = 0;
    }
}

BufferCore::~BufferCore() { release(); }

HRESULT KVertexBuffer::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = c.dev;
    c.dev->AddRef();
    return D3D_OK;
}

HRESULT KVertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *d)
{
    d->Format = D3DFMT_VERTEXDATA;
    d->Type = D3DRTYPE_VERTEXBUFFER;
    d->Usage = c.usage;
    d->Pool = c.pool;
    d->Size = c.size;
    d->FVF = fvf;
    return D3D_OK;
}

HRESULT KIndexBuffer::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = c.dev;
    c.dev->AddRef();
    return D3D_OK;
}

HRESULT KIndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *d)
{
    d->Format = fmt;
    d->Type = D3DRTYPE_INDEXBUFFER;
    d->Usage = c.usage;
    d->Pool = c.pool;
    d->Size = c.size;
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **pp, HANDLE *)
{
    auto *b = new KVertexBuffer();
    b->c.init(this, len, usage, pool, GL_ARRAY_BUFFER);
    b->fvf = fvf;
    *pp = b;
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9 **pp, HANDLE *)
{
    auto *b = new KIndexBuffer();
    b->c.init(this, len, usage, pool, GL_ELEMENT_ARRAY_BUFFER);
    b->fmt = fmt;
    *pp = b;
    return D3D_OK;
}

// ================================================================================================
// Declarations, shaders, queries
// ================================================================================================
HRESULT KDecl::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = dev;
    dev->AddRef();
    return D3D_OK;
}

HRESULT KDecl::GetDeclaration(D3DVERTEXELEMENT9 *e, UINT *n)
{
    if (!n)
        return D3DERR_INVALIDCALL;
    if (e)
    {
        for (size_t i = 0; i < el.size(); ++i)
            e[i] = el[i];
        const D3DVERTEXELEMENT9 end = D3DDECL_END();
        e[el.size()] = end;
    }
    *n = (UINT)el.size() + 1;
    return D3D_OK;
}

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **pp)
{
    auto *d = new KDecl();
    d->dev = this;
    for (const D3DVERTEXELEMENT9 *e = elements; e->Stream != 0xFF && d->el.size() < MAXD3DDECLLENGTH; ++e)
        d->el.push_back(*e);
    for (const D3DVERTEXELEMENT9 &e : d->el)
    {
        const int loc = sm3::AttribLocation(e.Usage, e.UsageIndex);
        if (e.Type == D3DDECLTYPE_D3DCOLOR && loc >= 0)
            d->bgraMask |= 1u << loc;
    }
    *pp = d;
    return D3D_OK;
}

static size_t ShaderTokenCount(const uint32_t *t)
{
    size_t n = 1;
    for (;;)
    {
        const uint32_t tok = t[n];
        const uint32_t op = tok & 0xFFFF;
        if (op == 0xFFFF)
            return n + 1;
        if (op == 0xFFFE)
            n += 1 + ((tok >> 16) & 0x7FFF);
        else
            n += 1 + ((tok >> 24) & 0xF);
        if (n > (1u << 20))
            return n;
    }
}

HRESULT KVShader::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = dev;
    dev->AddRef();
    return D3D_OK;
}

HRESULT KVShader::GetFunction(void *p, UINT *n)
{
    if (!n)
        return D3DERR_INVALIDCALL;
    if (p)
        memcpy(p, code.data(), std::min<size_t>(*n, code.size() * 4));
    *n = (UINT)(code.size() * 4);
    return D3D_OK;
}

void KVShader::OnZero()
{
    if (dev)
        dev->releaseProgramsFor(this);
    delete this;
}

HRESULT KPShader::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = dev;
    dev->AddRef();
    return D3D_OK;
}

HRESULT KPShader::GetFunction(void *p, UINT *n)
{
    if (!n)
        return D3DERR_INVALIDCALL;
    if (p)
        memcpy(p, code.data(), std::min<size_t>(*n, code.size() * 4));
    *n = (UINT)(code.size() * 4);
    return D3D_OK;
}

void KPShader::OnZero()
{
    if (dev)
        dev->releaseProgramsFor(this);
    delete this;
}

HRESULT Device::CreateVertexShader(const DWORD *fn, IDirect3DVertexShader9 **pp)
{
    auto *s = new KVShader();
    s->dev = this;
    s->code.assign(reinterpret_cast<const uint32_t *>(fn),
        reinterpret_cast<const uint32_t *>(fn) + ShaderTokenCount(reinterpret_cast<const uint32_t *>(fn)));
    if (!sm3::Translate(s->code.data(), s->tr))
        KLOG("vertex shader translation failed: %s", s->tr.error.c_str());
    *pp = s;
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(const DWORD *fn, IDirect3DPixelShader9 **pp)
{
    auto *s = new KPShader();
    s->dev = this;
    s->code.assign(reinterpret_cast<const uint32_t *>(fn),
        reinterpret_cast<const uint32_t *>(fn) + ShaderTokenCount(reinterpret_cast<const uint32_t *>(fn)));
    if (!sm3::Translate(s->code.data(), s->tr))
        KLOG("pixel shader translation failed: %s", s->tr.error.c_str());
    *pp = s;
    return D3D_OK;
}

HRESULT KQuery::GetDevice(IDirect3DDevice9 **pp)
{
    *pp = dev;
    dev->AddRef();
    return D3D_OK;
}

HRESULT KQuery::GetData(void *p, DWORD size, DWORD)
{
    // Queries answer immediately: a WebGL query result only becomes available once the render thread
    // returns to the event loop, and the engine spins on its fences. Occlusion reports "fully visible".
    if (p && size >= 4)
        *(DWORD *)p = type == D3DQUERYTYPE_OCCLUSION ? (1u << 24) : 1u;
    return D3D_OK;
}

HRESULT Device::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **pp)
{
    if (!pp)
        return D3D_OK; // capability probe
    if (type != D3DQUERYTYPE_EVENT && type != D3DQUERYTYPE_OCCLUSION)
        return D3DERR_NOTAVAILABLE;
    auto *q = new KQuery();
    q->dev = this;
    q->type = type;
    *pp = q;
    return D3D_OK;
}

} // namespace kd

void __cdecl R_StageDynamicVertexUpload(IDirect3DVertexBuffer9 *vb, const void *src, unsigned int offset, unsigned int bytes)
{
    if (!vb)
        return;
    static_cast<kd::KVertexBuffer *>(vb)->c.stageExternal(src, offset, bytes);
}

void __cdecl R_StageDynamicIndexUpload(IDirect3DIndexBuffer9 *ib, const void *src, unsigned int offset, unsigned int bytes)
{
    if (!ib)
        return;
    static_cast<kd::KIndexBuffer *>(ib)->c.stageExternal(src, offset, bytes);
}

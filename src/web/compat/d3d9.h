// Hand-written Direct3D 9 subset for the Web build (no DirectX SDK, no Wine headers).
//
// Contains the enums/structs/interfaces the engine references, with the SDK's numeric
// values and vtable method order: engine data stores some D3D enum values (formats in
// image assets, state) and the decompiled code contains raw vtable calls, so both must
// match the real API. In headless mode no device is ever created; this header is the
// seam where a WebGPU-backed device can be plugged in later.
//
// Exotic structs that the engine never touches (D3DCAPS9, D3DLIGHT9, ...) are only
// forward declared. If a new one is needed the web build fails to compile; add it.
#pragma once
// nvapi.h gates its D3D9 declarations on the SDK header guard.
#define _D3D9_H_
#define __d3d9_h__
#include <windows.h>

#define D3D_SDK_VERSION 32
#define DIRECT3D_VERSION 0x0900

#define D3DMAKEFOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a, r, g, b) \
    ((D3DCOLOR)((((a)&0xff) << 24) | (((r)&0xff) << 16) | (((g)&0xff) << 8) | ((b)&0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b) D3DCOLOR_ARGB(0xff, r, g, b)

// ----------------------------------------------------------------------------
// Errors
// ----------------------------------------------------------------------------
#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) MAKE_HRESULT(1, _FACD3D, code)
#define D3D_OK S_OK
#define D3DERR_WRONGTEXTUREFORMAT MAKE_D3DHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDALPHAOPERATION MAKE_D3DHRESULT(2074)
#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
#define D3DERR_NOTAVAILABLE MAKE_D3DHRESULT(2154)
#define D3DERR_OUTOFVIDEOMEMORY MAKE_D3DHRESULT(380)
#define D3DERR_DEVICELOST MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET MAKE_D3DHRESULT(2153)
#define D3DERR_DRIVERINTERNALERROR MAKE_D3DHRESULT(2087)
#define D3DERR_NOTFOUND MAKE_D3DHRESULT(2150)
#define D3DERR_MOREDATA MAKE_D3DHRESULT(2151)
#define D3DERR_DEVICEHUNG MAKE_D3DHRESULT(2164)
#define D3DERR_WASSTILLDRAWING MAKE_D3DHRESULT(540)
#define D3DERR_DEVICEREMOVED MAKE_D3DHRESULT(2160)
#define D3DOK_NOAUTOGEN MAKE_HRESULT(0, _FACD3D, 2159)
#define D3DDDIERR_DEVICEREMOVED MAKE_HRESULT(1, 0x876, 2160)

// ----------------------------------------------------------------------------
// Enums (SDK values)
// ----------------------------------------------------------------------------
typedef enum _D3DDEVTYPE {
    D3DDEVTYPE_HAL = 1, D3DDEVTYPE_REF = 2, D3DDEVTYPE_SW = 3, D3DDEVTYPE_NULLREF = 4,
    D3DDEVTYPE_FORCE_DWORD = 0x7fffffff
} D3DDEVTYPE;

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20, D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22, D3DFMT_R5G6B5 = 23,
    D3DFMT_X1R5G5B5 = 24, D3DFMT_A1R5G5B5 = 25, D3DFMT_A4R4G4B4 = 26, D3DFMT_R3G3B2 = 27,
    D3DFMT_A8 = 28, D3DFMT_A8R3G3B2 = 29, D3DFMT_X4R4G4B4 = 30, D3DFMT_A2B10G10R10 = 31,
    D3DFMT_A8B8G8R8 = 32, D3DFMT_X8B8G8R8 = 33, D3DFMT_G16R16 = 34, D3DFMT_A2R10G10B10 = 35,
    D3DFMT_A16B16G16R16 = 36,
    D3DFMT_A8P8 = 40, D3DFMT_P8 = 41,
    D3DFMT_L8 = 50, D3DFMT_A8L8 = 51, D3DFMT_A4L4 = 52,
    D3DFMT_V8U8 = 60, D3DFMT_L6V5U5 = 61, D3DFMT_X8L8V8U8 = 62, D3DFMT_Q8W8V8U8 = 63,
    D3DFMT_V16U16 = 64, D3DFMT_A2W10V10U10 = 67,
    D3DFMT_UYVY = D3DMAKEFOURCC('U', 'Y', 'V', 'Y'), D3DFMT_R8G8_B8G8 = D3DMAKEFOURCC('R', 'G', 'B', 'G'),
    D3DFMT_YUY2 = D3DMAKEFOURCC('Y', 'U', 'Y', '2'), D3DFMT_G8R8_G8B8 = D3DMAKEFOURCC('G', 'R', 'G', 'B'),
    D3DFMT_DXT1 = D3DMAKEFOURCC('D', 'X', 'T', '1'), D3DFMT_DXT2 = D3DMAKEFOURCC('D', 'X', 'T', '2'),
    D3DFMT_DXT3 = D3DMAKEFOURCC('D', 'X', 'T', '3'), D3DFMT_DXT4 = D3DMAKEFOURCC('D', 'X', 'T', '4'),
    D3DFMT_DXT5 = D3DMAKEFOURCC('D', 'X', 'T', '5'),
    D3DFMT_D16_LOCKABLE = 70, D3DFMT_D32 = 71, D3DFMT_D15S1 = 73, D3DFMT_D24S8 = 75,
    D3DFMT_D24X8 = 77, D3DFMT_D24X4S4 = 79, D3DFMT_D16 = 80, D3DFMT_D32F_LOCKABLE = 82,
    D3DFMT_D24FS8 = 83, D3DFMT_D32_LOCKABLE = 84, D3DFMT_S8_LOCKABLE = 85,
    D3DFMT_L16 = 81,
    D3DFMT_VERTEXDATA = 100, D3DFMT_INDEX16 = 101, D3DFMT_INDEX32 = 102,
    D3DFMT_Q16W16V16U16 = 110,
    D3DFMT_MULTI2_ARGB8 = D3DMAKEFOURCC('M', 'E', 'T', '1'),
    D3DFMT_R16F = 111, D3DFMT_G16R16F = 112, D3DFMT_A16B16G16R16F = 113,
    D3DFMT_R32F = 114, D3DFMT_G32R32F = 115, D3DFMT_A32B32G32R32F = 116,
    D3DFMT_CxV8U8 = 117,
    D3DFMT_FORCE_DWORD = 0x7fffffff
} D3DFORMAT;

typedef enum _D3DRESOURCETYPE {
    D3DRTYPE_SURFACE = 1, D3DRTYPE_VOLUME = 2, D3DRTYPE_TEXTURE = 3, D3DRTYPE_VOLUMETEXTURE = 4,
    D3DRTYPE_CUBETEXTURE = 5, D3DRTYPE_VERTEXBUFFER = 6, D3DRTYPE_INDEXBUFFER = 7,
    D3DRTYPE_FORCE_DWORD = 0x7fffffff
} D3DRESOURCETYPE;

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1, D3DPOOL_SYSTEMMEM = 2, D3DPOOL_SCRATCH = 3,
    D3DPOOL_FORCE_DWORD = 0x7fffffff
} D3DPOOL;

typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE = 0, D3DMULTISAMPLE_NONMASKABLE = 1, D3DMULTISAMPLE_2_SAMPLES = 2,
    D3DMULTISAMPLE_3_SAMPLES = 3, D3DMULTISAMPLE_4_SAMPLES = 4, D3DMULTISAMPLE_5_SAMPLES = 5,
    D3DMULTISAMPLE_6_SAMPLES = 6, D3DMULTISAMPLE_7_SAMPLES = 7, D3DMULTISAMPLE_8_SAMPLES = 8,
    D3DMULTISAMPLE_9_SAMPLES = 9, D3DMULTISAMPLE_10_SAMPLES = 10, D3DMULTISAMPLE_11_SAMPLES = 11,
    D3DMULTISAMPLE_12_SAMPLES = 12, D3DMULTISAMPLE_13_SAMPLES = 13, D3DMULTISAMPLE_14_SAMPLES = 14,
    D3DMULTISAMPLE_15_SAMPLES = 15, D3DMULTISAMPLE_16_SAMPLES = 16,
    D3DMULTISAMPLE_FORCE_DWORD = 0x7fffffff
} D3DMULTISAMPLE_TYPE;

typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD = 1, D3DSWAPEFFECT_FLIP = 2, D3DSWAPEFFECT_COPY = 3,
    D3DSWAPEFFECT_OVERLAY = 4, D3DSWAPEFFECT_FLIPEX = 5, D3DSWAPEFFECT_FORCE_DWORD = 0x7fffffff
} D3DSWAPEFFECT;

typedef enum _D3DBACKBUFFER_TYPE {
    D3DBACKBUFFER_TYPE_MONO = 0, D3DBACKBUFFER_TYPE_LEFT = 1, D3DBACKBUFFER_TYPE_RIGHT = 2,
    D3DBACKBUFFER_TYPE_FORCE_DWORD = 0x7fffffff
} D3DBACKBUFFER_TYPE;

typedef enum _D3DCUBEMAP_FACES {
    D3DCUBEMAP_FACE_POSITIVE_X = 0, D3DCUBEMAP_FACE_NEGATIVE_X = 1, D3DCUBEMAP_FACE_POSITIVE_Y = 2,
    D3DCUBEMAP_FACE_NEGATIVE_Y = 3, D3DCUBEMAP_FACE_POSITIVE_Z = 4, D3DCUBEMAP_FACE_NEGATIVE_Z = 5,
    D3DCUBEMAP_FACE_FORCE_DWORD = 0x7fffffff
} D3DCUBEMAP_FACES;

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1, D3DPT_LINELIST = 2, D3DPT_LINESTRIP = 3, D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6, D3DPT_FORCE_DWORD = 0x7fffffff
} D3DPRIMITIVETYPE;

typedef enum _D3DTEXTUREFILTERTYPE {
    D3DTEXF_NONE = 0, D3DTEXF_POINT = 1, D3DTEXF_LINEAR = 2, D3DTEXF_ANISOTROPIC = 3,
    D3DTEXF_PYRAMIDALQUAD = 6, D3DTEXF_GAUSSIANQUAD = 7, D3DTEXF_CONVOLUTIONMONO = 8,
    D3DTEXF_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREFILTERTYPE;

typedef enum _D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR = 2, D3DTADDRESS_CLAMP = 3, D3DTADDRESS_BORDER = 4,
    D3DTADDRESS_MIRRORONCE = 5, D3DTADDRESS_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREADDRESS;

typedef enum _D3DSAMPLERSTATETYPE {
    D3DSAMP_ADDRESSU = 1, D3DSAMP_ADDRESSV = 2, D3DSAMP_ADDRESSW = 3, D3DSAMP_BORDERCOLOR = 4,
    D3DSAMP_MAGFILTER = 5, D3DSAMP_MINFILTER = 6, D3DSAMP_MIPFILTER = 7, D3DSAMP_MIPMAPLODBIAS = 8,
    D3DSAMP_MAXMIPLEVEL = 9, D3DSAMP_MAXANISOTROPY = 10, D3DSAMP_SRGBTEXTURE = 11,
    D3DSAMP_ELEMENTINDEX = 12, D3DSAMP_DMAPOFFSET = 13, D3DSAMP_FORCE_DWORD = 0x7fffffff
} D3DSAMPLERSTATETYPE;

typedef enum _D3DRENDERSTATETYPE {
    D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8, D3DRS_SHADEMODE = 9, D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15, D3DRS_LASTPIXEL = 16, D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22, D3DRS_ZFUNC = 23, D3DRS_ALPHAREF = 24, D3DRS_ALPHAFUNC = 25,
    D3DRS_DITHERENABLE = 26, D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29, D3DRS_FOGCOLOR = 34, D3DRS_FOGTABLEMODE = 35, D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37, D3DRS_FOGDENSITY = 38, D3DRS_RANGEFOGENABLE = 48, D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53, D3DRS_STENCILZFAIL = 54, D3DRS_STENCILPASS = 55, D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57, D3DRS_STENCILMASK = 58, D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TEXTUREFACTOR = 60, D3DRS_WRAP0 = 128, D3DRS_CLIPPING = 136, D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139, D3DRS_FOGVERTEXMODE = 140, D3DRS_COLORVERTEX = 141,
    D3DRS_LOCALVIEWER = 142, D3DRS_NORMALIZENORMALS = 143, D3DRS_DIFFUSEMATERIALSOURCE = 145,
    D3DRS_SPECULARMATERIALSOURCE = 146, D3DRS_AMBIENTMATERIALSOURCE = 147,
    D3DRS_EMISSIVEMATERIALSOURCE = 148, D3DRS_VERTEXBLEND = 151, D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_POINTSIZE = 154, D3DRS_POINTSIZE_MIN = 155, D3DRS_POINTSPRITEENABLE = 156,
    D3DRS_POINTSCALEENABLE = 157, D3DRS_POINTSCALE_A = 158, D3DRS_POINTSCALE_B = 159,
    D3DRS_POINTSCALE_C = 160, D3DRS_MULTISAMPLEANTIALIAS = 161, D3DRS_MULTISAMPLEMASK = 162,
    D3DRS_PATCHEDGESTYLE = 163, D3DRS_DEBUGMONITORTOKEN = 165, D3DRS_POINTSIZE_MAX = 166,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167, D3DRS_COLORWRITEENABLE = 168, D3DRS_TWEENFACTOR = 170,
    D3DRS_BLENDOP = 171, D3DRS_POSITIONDEGREE = 172, D3DRS_NORMALDEGREE = 173,
    D3DRS_SCISSORTESTENABLE = 174, D3DRS_SLOPESCALEDEPTHBIAS = 175, D3DRS_ANTIALIASEDLINEENABLE = 176,
    D3DRS_MINTESSELLATIONLEVEL = 178, D3DRS_MAXTESSELLATIONLEVEL = 179, D3DRS_ADAPTIVETESS_X = 180,
    D3DRS_ADAPTIVETESS_Y = 181, D3DRS_ADAPTIVETESS_Z = 182, D3DRS_ADAPTIVETESS_W = 183,
    D3DRS_ENABLEADAPTIVETESSELLATION = 184, D3DRS_TWOSIDEDSTENCILMODE = 185,
    D3DRS_CCW_STENCILFAIL = 186, D3DRS_CCW_STENCILZFAIL = 187, D3DRS_CCW_STENCILPASS = 188,
    D3DRS_CCW_STENCILFUNC = 189, D3DRS_COLORWRITEENABLE1 = 190, D3DRS_COLORWRITEENABLE2 = 191,
    D3DRS_COLORWRITEENABLE3 = 192, D3DRS_BLENDFACTOR = 193, D3DRS_SRGBWRITEENABLE = 194,
    D3DRS_DEPTHBIAS = 195, D3DRS_WRAP8 = 198, D3DRS_SEPARATEALPHABLENDENABLE = 206,
    D3DRS_SRCBLENDALPHA = 207, D3DRS_DESTBLENDALPHA = 208, D3DRS_BLENDOPALPHA = 209,
    D3DRS_FORCE_DWORD = 0x7fffffff
} D3DRENDERSTATETYPE;

typedef enum _D3DZBUFFERTYPE { D3DZB_FALSE = 0, D3DZB_TRUE = 1, D3DZB_USEW = 2, D3DZB_FORCE_DWORD = 0x7fffffff } D3DZBUFFERTYPE;
typedef enum _D3DFILLMODE { D3DFILL_POINT = 1, D3DFILL_WIREFRAME = 2, D3DFILL_SOLID = 3, D3DFILL_FORCE_DWORD = 0x7fffffff } D3DFILLMODE;
typedef enum _D3DCULL { D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3, D3DCULL_FORCE_DWORD = 0x7fffffff } D3DCULL;
typedef enum _D3DCMPFUNC {
    D3DCMP_NEVER = 1, D3DCMP_LESS = 2, D3DCMP_EQUAL = 3, D3DCMP_LESSEQUAL = 4, D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6, D3DCMP_GREATEREQUAL = 7, D3DCMP_ALWAYS = 8, D3DCMP_FORCE_DWORD = 0x7fffffff
} D3DCMPFUNC;
typedef enum _D3DSTENCILOP {
    D3DSTENCILOP_KEEP = 1, D3DSTENCILOP_ZERO = 2, D3DSTENCILOP_REPLACE = 3, D3DSTENCILOP_INCRSAT = 4,
    D3DSTENCILOP_DECRSAT = 5, D3DSTENCILOP_INVERT = 6, D3DSTENCILOP_INCR = 7, D3DSTENCILOP_DECR = 8,
    D3DSTENCILOP_FORCE_DWORD = 0x7fffffff
} D3DSTENCILOP;
typedef enum _D3DBLEND {
    D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCCOLOR = 3, D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6, D3DBLEND_DESTALPHA = 7, D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9, D3DBLEND_INVDESTCOLOR = 10, D3DBLEND_SRCALPHASAT = 11,
    D3DBLEND_BOTHSRCALPHA = 12, D3DBLEND_BOTHINVSRCALPHA = 13, D3DBLEND_BLENDFACTOR = 14,
    D3DBLEND_INVBLENDFACTOR = 15, D3DBLEND_FORCE_DWORD = 0x7fffffff
} D3DBLEND;
typedef enum _D3DBLENDOP {
    D3DBLENDOP_ADD = 1, D3DBLENDOP_SUBTRACT = 2, D3DBLENDOP_REVSUBTRACT = 3, D3DBLENDOP_MIN = 4,
    D3DBLENDOP_MAX = 5, D3DBLENDOP_FORCE_DWORD = 0x7fffffff
} D3DBLENDOP;

typedef enum _D3DQUERYTYPE {
    D3DQUERYTYPE_VCACHE = 4, D3DQUERYTYPE_RESOURCEMANAGER = 5, D3DQUERYTYPE_VERTEXSTATS = 6,
    D3DQUERYTYPE_EVENT = 8, D3DQUERYTYPE_OCCLUSION = 9, D3DQUERYTYPE_TIMESTAMP = 10,
    D3DQUERYTYPE_TIMESTAMPDISJOINT = 11, D3DQUERYTYPE_TIMESTAMPFREQ = 12
} D3DQUERYTYPE;

typedef enum _D3DDECLTYPE {
    D3DDECLTYPE_FLOAT1 = 0, D3DDECLTYPE_FLOAT2 = 1, D3DDECLTYPE_FLOAT3 = 2, D3DDECLTYPE_FLOAT4 = 3,
    D3DDECLTYPE_D3DCOLOR = 4, D3DDECLTYPE_UBYTE4 = 5, D3DDECLTYPE_SHORT2 = 6, D3DDECLTYPE_SHORT4 = 7,
    D3DDECLTYPE_UBYTE4N = 8, D3DDECLTYPE_SHORT2N = 9, D3DDECLTYPE_SHORT4N = 10, D3DDECLTYPE_USHORT2N = 11,
    D3DDECLTYPE_USHORT4N = 12, D3DDECLTYPE_UDEC3 = 13, D3DDECLTYPE_DEC3N = 14, D3DDECLTYPE_FLOAT16_2 = 15,
    D3DDECLTYPE_FLOAT16_4 = 16, D3DDECLTYPE_UNUSED = 17
} D3DDECLTYPE;
typedef enum _D3DDECLMETHOD { D3DDECLMETHOD_DEFAULT = 0 } D3DDECLMETHOD;
typedef enum _D3DDECLUSAGE {
    D3DDECLUSAGE_POSITION = 0, D3DDECLUSAGE_BLENDWEIGHT = 1, D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL = 3, D3DDECLUSAGE_PSIZE = 4, D3DDECLUSAGE_TEXCOORD = 5, D3DDECLUSAGE_TANGENT = 6,
    D3DDECLUSAGE_BINORMAL = 7, D3DDECLUSAGE_TESSFACTOR = 8, D3DDECLUSAGE_POSITIONT = 9,
    D3DDECLUSAGE_COLOR = 10, D3DDECLUSAGE_FOG = 11, D3DDECLUSAGE_DEPTH = 12, D3DDECLUSAGE_SAMPLE = 13
} D3DDECLUSAGE;

// Usage / lock / clear / present flags
#define D3DUSAGE_RENDERTARGET 0x00000001L
#define D3DUSAGE_DEPTHSTENCIL 0x00000002L
#define D3DUSAGE_DYNAMIC 0x00000200L
#define D3DUSAGE_WRITEONLY 0x00000008L
#define D3DUSAGE_AUTOGENMIPMAP 0x00000400L
#define D3DLOCK_READONLY 0x00000010L
#define D3DLOCK_DISCARD 0x00002000L
#define D3DLOCK_NOOVERWRITE 0x00001000L
#define D3DLOCK_NOSYSLOCK 0x00000800L
#define D3DLOCK_DONOTWAIT 0x00004000L
#define D3DCLEAR_TARGET 0x00000001L
#define D3DCLEAR_ZBUFFER 0x00000002L
#define D3DCLEAR_STENCIL 0x00000004L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_MULTITHREADED 0x00000004L
#define D3DCREATE_FPU_PRESERVE 0x00000002L
#define D3DCOLORWRITEENABLE_RED (1L << 0)
#define D3DCOLORWRITEENABLE_GREEN (1L << 1)
#define D3DCOLORWRITEENABLE_BLUE (1L << 2)
#define D3DCOLORWRITEENABLE_ALPHA (1L << 3)
#define D3DISSUE_END (1 << 0)
#define D3DISSUE_BEGIN (1 << 1)
#define D3DGETDATA_FLUSH (1 << 0)
#define D3DPRESENT_INTERVAL_DEFAULT 0x00000000L
#define D3DPRESENT_INTERVAL_ONE 0x00000001L
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000L
#define D3DENUM_WHQL_LEVEL 0x00000002L
#define D3DADAPTER_DEFAULT 0
#define D3DSTREAMSOURCE_INDEXEDDATA (1 << 30)
#define D3DSTREAMSOURCE_INSTANCEDATA (2 << 30)
#define D3DDP_MAXTEXCOORD 8
#define MAXD3DDECLLENGTH 64
#define D3DDECL_END() {0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}

// ----------------------------------------------------------------------------
// Structures
// ----------------------------------------------------------------------------
typedef struct _D3DRECT { LONG x1, y1, x2, y2; } D3DRECT;
typedef struct _D3DVIEWPORT9 { DWORD X, Y, Width, Height; float MinZ, MaxZ; } D3DVIEWPORT9;
typedef struct _D3DLOCKED_RECT { INT Pitch; void *pBits; } D3DLOCKED_RECT;
typedef struct _D3DLOCKED_BOX { INT RowPitch; INT SlicePitch; void *pBits; } D3DLOCKED_BOX;
typedef struct _D3DBOX { UINT Left, Top, Right, Bottom, Front, Back; } D3DBOX;
typedef struct _D3DDISPLAYMODE { UINT Width, Height, RefreshRate; D3DFORMAT Format; } D3DDISPLAYMODE;
typedef struct _D3DRASTER_STATUS { BOOL InVBlank; UINT ScanLine; } D3DRASTER_STATUS;
typedef struct _D3DVERTEXELEMENT9 { WORD Stream; WORD Offset; BYTE Type; BYTE Method; BYTE Usage; BYTE UsageIndex; } D3DVERTEXELEMENT9, *LPD3DVERTEXELEMENT9;
typedef struct _D3DGAMMARAMP { WORD red[256], green[256], blue[256]; } D3DGAMMARAMP;
typedef struct _D3DSURFACE_DESC {
    D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool;
    D3DMULTISAMPLE_TYPE MultiSampleType; DWORD MultiSampleQuality; UINT Width; UINT Height;
} D3DSURFACE_DESC;
typedef struct _D3DVOLUME_DESC {
    D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Width, Height, Depth;
} D3DVOLUME_DESC;
typedef struct _D3DVERTEXBUFFER_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; DWORD FVF; } D3DVERTEXBUFFER_DESC;
typedef struct _D3DINDEXBUFFER_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; } D3DINDEXBUFFER_DESC;
typedef struct _D3DPRESENT_PARAMETERS_ {
    UINT BackBufferWidth, BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD MultiSampleQuality;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT PresentationInterval;
} D3DPRESENT_PARAMETERS;
typedef struct _D3DADAPTER_IDENTIFIER9 {
    char Driver[512]; char Description[512]; char DeviceName[32];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId, DeviceId, SubSysId, Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
} D3DADAPTER_IDENTIFIER9;

typedef struct _D3DVSHADERCAPS2_0 { DWORD Caps; INT DynamicFlowControlDepth; INT NumTemps; INT StaticFlowControlDepth; } D3DVSHADERCAPS2_0;
typedef struct _D3DPSHADERCAPS2_0 { DWORD Caps; INT DynamicFlowControlDepth; INT NumTemps; INT StaticFlowControlDepth; INT NumInstructionSlots; } D3DPSHADERCAPS2_0;
typedef struct _D3DCAPS9 {
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps, Caps2, Caps3;
    DWORD PresentationIntervals;
    DWORD CursorCaps;
    DWORD DevCaps;
    DWORD PrimitiveMiscCaps;
    DWORD RasterCaps;
    DWORD ZCmpCaps;
    DWORD SrcBlendCaps;
    DWORD DestBlendCaps;
    DWORD AlphaCmpCaps;
    DWORD ShadeCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD CubeTextureFilterCaps;
    DWORD VolumeTextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD VolumeTextureAddressCaps;
    DWORD LineCaps;
    DWORD MaxTextureWidth, MaxTextureHeight;
    DWORD MaxVolumeExtent;
    DWORD MaxTextureRepeat;
    DWORD MaxTextureAspectRatio;
    DWORD MaxAnisotropy;
    float MaxVertexW;
    float GuardBandLeft, GuardBandTop, GuardBandRight, GuardBandBottom;
    float ExtentsAdjust;
    DWORD StencilCaps;
    DWORD FVFCaps;
    DWORD TextureOpCaps;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float PixelShader1xMaxValue;
    DWORD DevCaps2;
    float MaxNpatchTessellationLevel;
    DWORD Reserved5;
    UINT MasterAdapterOrdinal;
    UINT AdapterOrdinalInGroup;
    UINT NumberOfAdaptersInGroup;
    DWORD DeclTypes;
    DWORD NumSimultaneousRTs;
    DWORD StretchRectFilterCaps;
    D3DVSHADERCAPS2_0 VS20Caps;
    D3DPSHADERCAPS2_0 PS20Caps;
    DWORD VertexTextureFilterCaps;
    DWORD MaxVShaderInstructionsExecuted;
    DWORD MaxPShaderInstructionsExecuted;
    DWORD MaxVertexShader30InstructionSlots;
    DWORD MaxPixelShader30InstructionSlots;
} D3DCAPS9;struct D3DMATRIX;
struct D3DMATERIAL9;
struct D3DLIGHT9;
struct D3DCLIPSTATUS9;
struct D3DRECTPATCH_INFO;
struct D3DTRIPATCH_INFO;
struct D3DDEVICE_CREATION_PARAMETERS;
struct D3DINDEXBUFFER_DESC_;
// Fixed-function enums: the engine never uses them; the values exist so the device class can implement
// the (ignored) fixed-function entry points.
typedef enum _D3DTRANSFORMSTATETYPE : int { D3DTS_FORCE_DWORD = 0x7fffffff } D3DTRANSFORMSTATETYPE;
typedef enum _D3DTEXTURESTAGESTATETYPE : int { D3DTSS_FORCE_DWORD = 0x7fffffff } D3DTEXTURESTAGESTATETYPE;
typedef enum _D3DSTATEBLOCKTYPE : int { D3DSBT_FORCE_DWORD = 0x7fffffff } D3DSTATEBLOCKTYPE;
typedef enum _D3DBACKBUFFER_TYPE D3DBACKBUFFER_TYPE_;

typedef struct PALETTEENTRY_ { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;

// ----------------------------------------------------------------------------
// Interfaces (SDK vtable order)
// ----------------------------------------------------------------------------
interface IDirect3DDevice9;
interface IDirect3DSurface9;
interface IDirect3DVolume9;
interface IDirect3DSwapChain9;
interface IDirect3DVertexBuffer9;
interface IDirect3DIndexBuffer9;
interface IDirect3DVertexDeclaration9;
interface IDirect3DVertexShader9;
interface IDirect3DPixelShader9;
interface IDirect3DStateBlock9;
interface IDirect3DQuery9;
interface IDirect3DBaseTexture9;
interface IDirect3DTexture9;
interface IDirect3DCubeTexture9;
interface IDirect3DVolumeTexture9;

interface IDirect3D9 : public IUnknown
{
    virtual HRESULT RegisterSoftwareDevice(void *pInitializeFunction) = 0;
    virtual UINT GetAdapterCount() = 0;
    virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier) = 0;
    virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) = 0;
    virtual HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) = 0;
    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels) = 0;
    virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
    virtual HRESULT CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) = 0;
    virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) = 0;
    virtual HMONITOR GetAdapterMonitor(UINT Adapter) = 0;
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface) = 0;
};
typedef IDirect3D9 *LPDIRECT3D9, *PDIRECT3D9;

interface IDirect3DResource9 : public IUnknown
{
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT SetPrivateData(REFGUID refguid, const void *pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) = 0;
    virtual HRESULT FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD GetPriority() = 0;
    virtual void PreLoad() = 0;
    virtual D3DRESOURCETYPE GetType() = 0;
};

interface IDirect3DSurface9 : public IDirect3DResource9
{
    virtual HRESULT GetContainer(REFIID riid, void **ppContainer) = 0;
    virtual HRESULT GetDesc(D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect() = 0;
    virtual HRESULT GetDC(HDC *phdc) = 0;
    virtual HRESULT ReleaseDC(HDC hdc) = 0;
};
typedef IDirect3DSurface9 *LPDIRECT3DSURFACE9, *PDIRECT3DSURFACE9;

interface IDirect3DBaseTexture9 : public IDirect3DResource9
{
    virtual DWORD SetLOD(DWORD LODNew) = 0;
    virtual DWORD GetLOD() = 0;
    virtual DWORD GetLevelCount() = 0;
    virtual HRESULT SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) = 0;
    virtual D3DTEXTUREFILTERTYPE GetAutoGenFilterType() = 0;
    virtual void GenerateMipSubLevels() = 0;
};
typedef IDirect3DBaseTexture9 *LPDIRECT3DBASETEXTURE9;

interface IDirect3DTexture9 : public IDirect3DBaseTexture9
{
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) = 0;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(UINT Level) = 0;
    virtual HRESULT AddDirtyRect(const RECT *pDirtyRect) = 0;
};
typedef IDirect3DTexture9 *LPDIRECT3DTEXTURE9, *PDIRECT3DTEXTURE9;

interface IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9
{
    virtual HRESULT GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) = 0;
    virtual HRESULT GetVolumeLevel(UINT Level, IDirect3DVolume9 **ppVolumeLevel) = 0;
    virtual HRESULT LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) = 0;
    virtual HRESULT UnlockBox(UINT Level) = 0;
    virtual HRESULT AddDirtyBox(const D3DBOX *pDirtyBox) = 0;
};
typedef IDirect3DVolumeTexture9 *LPDIRECT3DVOLUMETEXTURE9;

interface IDirect3DCubeTexture9 : public IDirect3DBaseTexture9
{
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9 **ppCubeMapSurface) = 0;
    virtual HRESULT LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) = 0;
    virtual HRESULT AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT *pDirtyRect) = 0;
};
typedef IDirect3DCubeTexture9 *LPDIRECT3DCUBETEXTURE9;

interface IDirect3DVertexBuffer9 : public IDirect3DResource9
{
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) = 0;
};
typedef IDirect3DVertexBuffer9 *LPDIRECT3DVERTEXBUFFER9;

interface IDirect3DIndexBuffer9 : public IDirect3DResource9
{
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) = 0;
};
typedef IDirect3DIndexBuffer9 *LPDIRECT3DINDEXBUFFER9;

interface IDirect3DVertexDeclaration9 : public IUnknown
{
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetDeclaration(D3DVERTEXELEMENT9 *pElement, UINT *pNumElements) = 0;
};
typedef IDirect3DVertexDeclaration9 *LPDIRECT3DVERTEXDECLARATION9;

interface IDirect3DVertexShader9 : public IUnknown
{
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetFunction(void *pData, UINT *pSizeOfData) = 0;
};
typedef IDirect3DVertexShader9 *LPDIRECT3DVERTEXSHADER9;

interface IDirect3DPixelShader9 : public IUnknown
{
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetFunction(void *pData, UINT *pSizeOfData) = 0;
};
typedef IDirect3DPixelShader9 *LPDIRECT3DPIXELSHADER9;

interface IDirect3DQuery9 : public IUnknown
{
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual D3DQUERYTYPE GetType() = 0;
    virtual DWORD GetDataSize() = 0;
    virtual HRESULT Issue(DWORD dwIssueFlags) = 0;
    virtual HRESULT GetData(void *pData, DWORD dwSize, DWORD dwGetDataFlags) = 0;
};
typedef IDirect3DQuery9 *LPDIRECT3DQUERY9;

interface IDirect3DSwapChain9 : public IUnknown
{
    virtual HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const void *pDirtyRegion, DWORD dwFlags) = 0;
    virtual HRESULT GetFrontBufferData(IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) = 0;
    virtual HRESULT GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) = 0;
    virtual HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
};
typedef IDirect3DSwapChain9 *LPDIRECT3DSWAPCHAIN9;

interface IDirect3DDevice9 : public IUnknown
{
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT EvictManagedResources() = 0;
    virtual HRESULT GetDirect3D(IDirect3D9 **ppD3D9) = 0;
    virtual HRESULT GetDeviceCaps(D3DCAPS9 *pCaps) = 0;
    virtual HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) = 0;
    virtual HRESULT SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap) = 0;
    virtual void SetCursorPosition(int X, int Y, DWORD Flags) = 0;
    virtual BOOL ShowCursor(BOOL bShow) = 0;
    virtual HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DSwapChain9 **pSwapChain) = 0;
    virtual HRESULT GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9 **pSwapChain) = 0;
    virtual UINT GetNumberOfSwapChains() = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
    virtual HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const void *pDirtyRegion) = 0;
    virtual HRESULT GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) = 0;
    virtual HRESULT GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS *pRasterStatus) = 0;
    virtual HRESULT SetDialogBoxMode(BOOL bEnableDialogs) = 0;
    virtual void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP *pRamp) = 0;
    virtual void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp) = 0;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle) = 0;
    virtual HRESULT UpdateSurface(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestinationSurface, const POINT *pDestPoint) = 0;
    virtual HRESULT UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture) = 0;
    virtual HRESULT GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT StretchRect(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestSurface, const RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter) = 0;
    virtual HRESULT ColorFill(IDirect3DSurface9 *pSurface, const RECT *pRect, D3DCOLOR color) = 0;
    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle) = 0;
    virtual HRESULT SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget) = 0;
    virtual HRESULT GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget) = 0;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pNewZStencil) = 0;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9 **ppZStencilSurface) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) = 0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) = 0;
    virtual HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) = 0;
    virtual HRESULT GetViewport(D3DVIEWPORT9 *pViewport) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9 *pMaterial) = 0;
    virtual HRESULT GetMaterial(D3DMATERIAL9 *pMaterial) = 0;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) = 0;
    virtual HRESULT GetLight(DWORD Index, D3DLIGHT9 *pLight) = 0;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) = 0;
    virtual HRESULT SetClipPlane(DWORD Index, const float *pPlane) = 0;
    virtual HRESULT GetClipPlane(DWORD Index, float *pPlane) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) = 0;
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB) = 0;
    virtual HRESULT BeginStateBlock() = 0;
    virtual HRESULT EndStateBlock(IDirect3DStateBlock9 **ppSB) = 0;
    virtual HRESULT SetClipStatus(const D3DCLIPSTATUS9 *pClipStatus) = 0;
    virtual HRESULT GetClipStatus(D3DCLIPSTATUS9 *pClipStatus) = 0;
    virtual HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTexture) = 0;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTexture) = 0;
    virtual HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) = 0;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue) = 0;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT ValidateDevice(DWORD *pNumPasses) = 0;
    virtual HRESULT SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY *pEntries) = 0;
    virtual HRESULT GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY *pEntries) = 0;
    virtual HRESULT SetCurrentTexturePalette(UINT PaletteNumber) = 0;
    virtual HRESULT GetCurrentTexturePalette(UINT *PaletteNumber) = 0;
    virtual HRESULT SetScissorRect(const RECT *pRect) = 0;
    virtual HRESULT GetScissorRect(RECT *pRect) = 0;
    virtual HRESULT SetSoftwareVertexProcessing(BOOL bSoftware) = 0;
    virtual BOOL GetSoftwareVertexProcessing() = 0;
    virtual HRESULT SetNPatchMode(float nSegments) = 0;
    virtual float GetNPatchMode() = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9 *pDestBuffer, IDirect3DVertexDeclaration9 *pVertexDecl, DWORD Flags) = 0;
    virtual HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements, IDirect3DVertexDeclaration9 **ppDecl) = 0;
    virtual HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl) = 0;
    virtual HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **ppDecl) = 0;
    virtual HRESULT SetFVF(DWORD FVF) = 0;
    virtual HRESULT GetFVF(DWORD *pFVF) = 0;
    virtual HRESULT CreateVertexShader(const DWORD *pFunction, IDirect3DVertexShader9 **ppShader) = 0;
    virtual HRESULT SetVertexShader(IDirect3DVertexShader9 *pShader) = 0;
    virtual HRESULT GetVertexShader(IDirect3DVertexShader9 **ppShader) = 0;
    virtual HRESULT SetVertexShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT GetVertexShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT SetVertexShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT GetVertexShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT SetVertexShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT GetVertexShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes, UINT Stride) = 0;
    virtual HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 **ppStreamData, UINT *pOffsetInBytes, UINT *pStride) = 0;
    virtual HRESULT SetStreamSourceFreq(UINT StreamNumber, UINT Setting) = 0;
    virtual HRESULT GetStreamSourceFreq(UINT StreamNumber, UINT *pSetting) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexData) = 0;
    virtual HRESULT GetIndices(IDirect3DIndexBuffer9 **ppIndexData) = 0;
    virtual HRESULT CreatePixelShader(const DWORD *pFunction, IDirect3DPixelShader9 **ppShader) = 0;
    virtual HRESULT SetPixelShader(IDirect3DPixelShader9 *pShader) = 0;
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9 **ppShader) = 0;
    virtual HRESULT SetPixelShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT GetPixelShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT SetPixelShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT GetPixelShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT SetPixelShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT GetPixelShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT DrawRectPatch(UINT Handle, const float *pNumSegs, const D3DRECTPATCH_INFO *pRectPatchInfo) = 0;
    virtual HRESULT DrawTriPatch(UINT Handle, const float *pNumSegs, const D3DTRIPATCH_INFO *pTriPatchInfo) = 0;
    virtual HRESULT DeletePatch(UINT Handle) = 0;
    virtual HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9 **ppQuery) = 0;
};
typedef IDirect3DDevice9 *LPDIRECT3DDEVICE9, *PDIRECT3DDEVICE9;

// PIX markers (no-ops: there is no PIX on the web)
inline DWORD D3DPERF_BeginEvent(D3DCOLOR, const WCHAR *) { return 0; }
inline DWORD D3DPERF_EndEvent() { return 0; }
inline void D3DPERF_SetMarker(D3DCOLOR, const WCHAR *) {}
inline void D3DPERF_SetRegion(D3DCOLOR, const WCHAR *) {}
inline BOOL D3DPERF_QueryRepeatFrame() { return FALSE; }
inline void D3DPERF_SetOptions(DWORD) {}
inline DWORD D3DPERF_GetStatus() { return 0; }

// Implemented by the WebGL2 backend in src/web/gl/web_d3d9_device.cpp. It returns nullptr under a
// dedicated server (no WebGL context is ever requested there).
IDirect3D9 *Direct3DCreate9(UINT);

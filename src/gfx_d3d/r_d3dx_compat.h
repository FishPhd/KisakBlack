#pragma once

// D3DX9 compatibility layer.
//
// Normal build (KISAK_NO_D3DX not defined): just pulls in the real <d3dx9.h> (needs d3dx9_43.dll at runtime).
//
// KISAK_NO_D3DX build (CMake option, default ON): the handful of D3DX9 entry points the engine uses are
// provided here, so d3dx9_43.dll / d3dx9d_43.dll is not imported:
//   - ID3DXBuffer / D3DXCreateBuffer         -> small local blob class
//   - D3DXCompileShader                      -> D3DCompile from d3dcompiler_47.dll, loaded dynamically
//                                               (system DLL on Windows 8.1+; failure => compile returns E_FAIL)
//   - D3DXGetShaderConstantTable             -> parses the CTAB comment of the shader byte code
//   - D3DXGetShaderInputSemantics/Output...  -> parses the dcl instructions of the shader byte code
// Screenshot saving (D3DXSaveSurfaceToFileA) is replaced in r_screenshot.cpp.

#ifndef KISAK_NO_D3DX

#include <d3dx9.h>

#else // KISAK_NO_D3DX

#include <d3d9.h>

#ifndef D3DXERR_INVALIDDATA
#define D3DXERR_INVALIDDATA ((HRESULT)0x88760b59L) // MAKE_DDHRESULT(2905)
#endif

// ---- buffers ---------------------------------------------------------------------------------------------
// Same vtable layout as the real ID3DXBuffer (and as ID3DBlob returned by D3DCompile).
struct ID3DXBuffer : public IUnknown
{
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef ID3DXBuffer *LPD3DXBUFFER;

HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer);

// ---- shader compile ----------------------------------------------------------------------------------------
struct ID3DXConstantTable;
typedef ID3DXConstantTable *LPD3DXCONSTANTTABLE;
typedef struct _D3DXMACRO
{
    LPCSTR Name;
    LPCSTR Definition;
} D3DXMACRO, *LPD3DXMACRO;
struct ID3DXInclude;
typedef ID3DXInclude *LPD3DXINCLUDE;

#define D3DXSHADER_DEBUG                          (1 << 0)
#define D3DXSHADER_SKIPVALIDATION                 (1 << 1)
#define D3DXSHADER_SKIPOPTIMIZATION               (1 << 2)

HRESULT WINAPI D3DXCompileShader(
    LPCSTR pSrcData,
    UINT SrcDataLen,
    const D3DXMACRO *pDefines,
    LPD3DXINCLUDE pInclude,
    LPCSTR pFunctionName,
    LPCSTR pProfile,
    DWORD Flags,
    LPD3DXBUFFER *ppShader,
    LPD3DXBUFFER *ppErrorMsgs,
    LPD3DXCONSTANTTABLE *ppConstantTable);

// ---- constant table (CTAB) -----------------------------------------------------------------------------------
typedef struct _D3DXSHADER_CONSTANTTABLE
{
    DWORD Size;             // sizeof(D3DXSHADER_CONSTANTTABLE)
    DWORD Creator;          // LPCSTR offset
    DWORD Version;          // shader version
    DWORD Constants;        // number of constants
    DWORD ConstantInfo;     // D3DXSHADER_CONSTANTINFO[Constants] offset
    DWORD Flags;            // flags shader was compiled with
    DWORD Target;           // LPCSTR offset
} D3DXSHADER_CONSTANTTABLE, *LPD3DXSHADER_CONSTANTTABLE;

typedef struct _D3DXSHADER_CONSTANTINFO
{
    DWORD Name;             // LPCSTR offset
    WORD  RegisterSet;      // D3DXREGISTER_SET
    WORD  RegisterIndex;    // register number
    WORD  RegisterCount;    // number of registers
    WORD  Reserved;         // reserved
    DWORD TypeInfo;         // D3DXSHADER_TYPEINFO offset
    DWORD DefaultValue;     // offset of default value
} D3DXSHADER_CONSTANTINFO, *LPD3DXSHADER_CONSTANTINFO;

// Minimal stand-in for ID3DXConstantTable: only GetBufferPointer()/Release() are used by the engine.
// GetBufferPointer() returns the D3DXSHADER_CONSTANTTABLE inside the shader byte code (not a copy).
struct ID3DXConstantTable
{
    const void *ctab;
    LPVOID GetBufferPointer() { return (LPVOID)ctab; }
    ULONG Release() { delete this; return 0; }
};

HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *pFunction, LPD3DXCONSTANTTABLE *ppConstantTable);

// ---- semantics -----------------------------------------------------------------------------------------------
typedef struct _D3DXSEMANTIC
{
    UINT Usage;             // D3DDECLUSAGE
    UINT UsageIndex;
} D3DXSEMANTIC, *LPD3DXSEMANTIC;

HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);
HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);

// ---- image file formats (values match the real enum; only JPG/TGA are produced by the engine) -----------------
typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_HDR = 7,
    D3DXIFF_PFM = 8,
    D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

#endif // KISAK_NO_D3DX

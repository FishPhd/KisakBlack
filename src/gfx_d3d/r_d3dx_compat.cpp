#ifdef KISAK_NO_D3DX

#include "r_d3dx_compat.h"
#include <new>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Set env KISAK_D3DX_TRACE=1 to log (to d3dx_compat_trace.txt in the cwd) which replacements are actually used.
static void KisakD3DXTrace(const char *what, HRESULT hr)
{
    static int s_enabled = -1;
    if (s_enabled < 0)
        s_enabled = getenv("KISAK_D3DX_TRACE") ? 1 : 0;
    if (!s_enabled)
        return;
    FILE *f = fopen("d3dx_compat_trace.txt", "a");
    if (f)
    {
        fprintf(f, "%s hr=0x%08x\n", what, (unsigned)hr);
        fclose(f);
    }
}

// ------------------------------------------------------------------------------------------------------------
// Buffer
// ------------------------------------------------------------------------------------------------------------
namespace
{
    class KisakBuffer final : public ID3DXBuffer
    {
    public:
        explicit KisakBuffer(DWORD size) : m_refs(1), m_size(size), m_data(nullptr)
        {
            m_data = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
        }
        ~KisakBuffer() { if (m_data) HeapFree(GetProcessHeap(), 0, m_data); }
        bool Valid() const { return m_data != nullptr; }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
        {
            if (!ppv)
                return E_POINTER;
            if (riid == IID_IUnknown)
            {
                *ppv = static_cast<IUnknown *>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            LONG r = InterlockedDecrement(&m_refs);
            if (r == 0)
                delete this;
            return (ULONG)r;
        }
        LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return m_data; }
        DWORD STDMETHODCALLTYPE GetBufferSize() override { return m_size; }

    private:
        volatile LONG m_refs;
        DWORD m_size;
        BYTE *m_data;
    };
}

HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer)
{
    if (!ppBuffer)
        return D3DERR_INVALIDCALL;
    *ppBuffer = nullptr;
    KisakBuffer *buf = new (std::nothrow) KisakBuffer(NumBytes);
    if (!buf || !buf->Valid())
    {
        delete buf;
        return E_OUTOFMEMORY;
    }
    *ppBuffer = buf;
    KisakD3DXTrace("D3DXCreateBuffer", S_OK);
    return S_OK;
}

// ------------------------------------------------------------------------------------------------------------
// Shader compile (D3DCompile from d3dcompiler_47.dll, loaded on first use)
// ------------------------------------------------------------------------------------------------------------
typedef HRESULT(WINAPI *PFN_D3DCompile)(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const void *pDefines,
    void *pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DXBuffer **ppCode,
    ID3DXBuffer **ppErrorMsgs);

static PFN_D3DCompile KisakD3DX_GetCompiler()
{
    static PFN_D3DCompile s_compile = nullptr;
    static bool s_tried = false;
    if (!s_tried)
    {
        s_tried = true;
        static const char *dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
        for (const char *dll : dlls)
        {
            HMODULE mod = LoadLibraryA(dll);
            if (mod)
            {
                s_compile = (PFN_D3DCompile)GetProcAddress(mod, "D3DCompile");
                if (s_compile)
                    break;
            }
        }
        if (!s_compile)
            OutputDebugStringA("r_d3dx_compat: no d3dcompiler_xx.dll found; runtime HLSL compilation unavailable\n");
    }
    return s_compile;
}

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
    LPD3DXCONSTANTTABLE *ppConstantTable)
{
    if (ppShader)
        *ppShader = nullptr;
    if (ppErrorMsgs)
        *ppErrorMsgs = nullptr;
    if (ppConstantTable)
        *ppConstantTable = nullptr;

    PFN_D3DCompile compile = KisakD3DX_GetCompiler();
    if (!compile)
        return E_FAIL;

    // D3DXSHADER_USE_LEGACY_D3DX9_31_DLL (1<<16) has no D3DCompile equivalent.
    const UINT flags = Flags & ~(1u << 16);

    ID3DXBuffer *code = nullptr;
    ID3DXBuffer *errors = nullptr;
    HRESULT hr = compile(pSrcData, SrcDataLen, nullptr, pDefines, pInclude, pFunctionName, pProfile, flags, 0, &code, &errors);

    if (ppErrorMsgs)
        *ppErrorMsgs = errors;
    else if (errors)
        errors->Release();

    if (FAILED(hr))
    {
        if (code)
            code->Release();
        return hr;
    }

    if (ppConstantTable && code)
    {
        if (FAILED(D3DXGetShaderConstantTable((const DWORD *)code->GetBufferPointer(), ppConstantTable)))
            *ppConstantTable = nullptr;
    }
    if (ppShader)
        *ppShader = code;
    else if (code)
        code->Release();
    KisakD3DXTrace("D3DXCompileShader", hr);
    return hr;
}

// ------------------------------------------------------------------------------------------------------------
// Shader byte code parsing
// ------------------------------------------------------------------------------------------------------------
namespace
{
    const DWORD KISAK_D3DSIO_COMMENT = 0xFFFE;
    const DWORD KISAK_D3DSIO_END = 0xFFFF;
    const DWORD KISAK_D3DSIO_DCL = 0x1F;
    const DWORD KISAK_FOURCC_CTAB = 0x42415443; // 'CTAB'

    const DWORD KISAK_D3DSPR_INPUT = 1;
    const DWORD KISAK_D3DSPR_OUTPUT = 6; // TEXCRDOUT / OUTPUT in vs_3_0

    inline bool KisakIsShaderVersion(DWORD token)
    {
        const DWORD kind = token & 0xFFFF0000;
        return kind == 0xFFFE0000 || kind == 0xFFFF0000;
    }

    // Walks the instruction stream. visit(opcode, tokens, length) is called for each non-comment instruction.
    template <typename F>
    bool KisakWalkShader(const DWORD *fn, F visit)
    {
        if (!fn || !KisakIsShaderVersion(fn[0]))
            return false;
        const DWORD *p = fn + 1;
        // Hard limit so a corrupt stream can't run away.
        const DWORD *limit = fn + (1u << 20);
        while (p < limit)
        {
            const DWORD token = *p;
            const DWORD opcode = token & 0xFFFF;
            if (opcode == KISAK_D3DSIO_END)
                return true;
            if (opcode == KISAK_D3DSIO_COMMENT)
            {
                p += 1 + ((token >> 16) & 0x7FFF);
                continue;
            }
            const DWORD length = (token >> 24) & 0xF; // SM2+: number of operand tokens
            visit(opcode, p + 1, length);
            p += 1 + length;
        }
        return false;
    }

    HRESULT KisakGetSemantics(const DWORD *fn, D3DXSEMANTIC *out, UINT *count, DWORD regType)
    {
        if (!count)
            return D3DERR_INVALIDCALL;
        UINT n = 0;
        const bool ok = KisakWalkShader(fn, [&](DWORD opcode, const DWORD *tokens, DWORD length)
        {
            if (opcode != KISAK_D3DSIO_DCL || length < 2)
                return;
            const DWORD dclInfo = tokens[0];
            const DWORD dst = tokens[1];
            const DWORD type = ((dst >> 28) & 0x7) | ((dst >> 8) & 0x18);
            if (type != regType)
                return;
            if (out)
            {
                out[n].Usage = dclInfo & 0x1F;
                out[n].UsageIndex = (dclInfo >> 16) & 0xF;
            }
            ++n;
        });
        if (!ok)
            return D3DERR_INVALIDCALL;
        *count = n;
        return S_OK;
    }
}

HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *pFunction, LPD3DXCONSTANTTABLE *ppConstantTable)
{
    if (!ppConstantTable)
        return D3DERR_INVALIDCALL;
    *ppConstantTable = nullptr;
    if (!pFunction || !KisakIsShaderVersion(pFunction[0]))
        return D3DERR_INVALIDCALL;

    const DWORD *p = pFunction + 1;
    const DWORD *limit = pFunction + (1u << 20);
    while (p < limit)
    {
        const DWORD token = *p;
        const DWORD opcode = token & 0xFFFF;
        if (opcode == KISAK_D3DSIO_END)
            break;
        if (opcode == KISAK_D3DSIO_COMMENT)
        {
            const DWORD size = (token >> 16) & 0x7FFF;
            if (size >= 1 + sizeof(D3DXSHADER_CONSTANTTABLE) / 4 && p[1] == KISAK_FOURCC_CTAB)
            {
                ID3DXConstantTable *table = new (std::nothrow) ID3DXConstantTable;
                if (!table)
                    return E_OUTOFMEMORY;
                table->ctab = p + 2;
                *ppConstantTable = table;
                KisakD3DXTrace("D3DXGetShaderConstantTable", S_OK);
                return S_OK;
            }
            p += 1 + size;
            continue;
        }
        p += 1 + ((token >> 24) & 0xF);
    }
    return D3DXERR_INVALIDDATA;
}

HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount)
{
    KisakD3DXTrace("D3DXGetShaderInputSemantics", S_OK);
    return KisakGetSemantics(pFunction, pSemantics, pCount, KISAK_D3DSPR_INPUT);
}

HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount)
{
    KisakD3DXTrace("D3DXGetShaderOutputSemantics", S_OK);
    if (pFunction && pCount && (pFunction[0] & 0xFFFF0000) == 0xFFFF0000)
    {
        // Pixel shaders have no output dcl; D3DX reports the single colour output (COLOR0).
        if (pSemantics)
        {
            pSemantics[0].Usage = D3DDECLUSAGE_COLOR;
            pSemantics[0].UsageIndex = 0;
        }
        *pCount = 1;
        return S_OK;
    }
    return KisakGetSemantics(pFunction, pSemantics, pCount, KISAK_D3DSPR_OUTPUT);
}

#endif // KISAK_NO_D3DX

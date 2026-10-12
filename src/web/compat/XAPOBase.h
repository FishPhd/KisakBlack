// XAPO (XAudio2 audio processing object) subset for the Web build. See XAudio2.h.
#pragma once
#include <windows.h>

#define XAPO_FLAG_CHANNELS_MUST_MATCH 0x00000001
#define XAPO_FLAG_FRAMERATE_MUST_MATCH 0x00000002
#define XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH 0x00000004
#define XAPO_FLAG_BUFFERCOUNT_MUST_MATCH 0x00000008
#define XAPO_FLAG_INPLACE_REQUIRED 0x00000020
#define XAPO_FLAG_INPLACE_SUPPORTED 0x00000010
#define XAPO_REGISTRATION_STRING_LENGTH 256

struct WAVEFORMATEX_;
struct tWAVEFORMATEX;

typedef struct XAPO_REGISTRATION_PROPERTIES {
    CLSID clsid;
    WCHAR FriendlyName[XAPO_REGISTRATION_STRING_LENGTH];
    WCHAR CopyrightInfo[XAPO_REGISTRATION_STRING_LENGTH];
    UINT32 MajorVersion;
    UINT32 MinorVersion;
    UINT32 Flags;
    UINT32 MinInputBufferCount;
    UINT32 MaxInputBufferCount;
    UINT32 MinOutputBufferCount;
    UINT32 MaxOutputBufferCount;
} XAPO_REGISTRATION_PROPERTIES;

typedef struct XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS {
    const tWAVEFORMATEX *pFormat;
    UINT32 MaxFrameCount;
} XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS;

typedef enum XAPO_BUFFER_FLAGS { XAPO_BUFFER_SILENT, XAPO_BUFFER_VALID } XAPO_BUFFER_FLAGS;

typedef struct XAPO_PROCESS_BUFFER_PARAMETERS {
    void *pBuffer;
    XAPO_BUFFER_FLAGS BufferFlags;
    UINT32 ValidFrameCount;
} XAPO_PROCESS_BUFFER_PARAMETERS;

interface IXAPO : public IUnknown
{
    virtual HRESULT GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **ppRegistrationProperties) = 0;
    virtual HRESULT IsInputFormatSupported(const tWAVEFORMATEX *pOutputFormat, const tWAVEFORMATEX *pRequestedInputFormat, tWAVEFORMATEX **ppSupportedInputFormat) = 0;
    virtual HRESULT IsOutputFormatSupported(const tWAVEFORMATEX *pInputFormat, const tWAVEFORMATEX *pRequestedOutputFormat, tWAVEFORMATEX **ppSupportedOutputFormat) = 0;
    virtual HRESULT Initialize(const void *pData, UINT32 DataByteSize) = 0;
    virtual void Reset() = 0;
    virtual HRESULT LockForProcess(UINT32 InputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pInputLockedParameters, UINT32 OutputLockedParameterCount, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pOutputLockedParameters) = 0;
    virtual void UnlockForProcess() = 0;
    virtual void Process(UINT32 InputProcessParameterCount, const XAPO_PROCESS_BUFFER_PARAMETERS *pInputProcessParameters, UINT32 OutputProcessParameterCount, XAPO_PROCESS_BUFFER_PARAMETERS *pOutputProcessParameters, BOOL IsEnabled) = 0;
    virtual UINT32 CalcInputFrames(UINT32 OutputFrameCount) = 0;
    virtual UINT32 CalcOutputFrames(UINT32 InputFrameCount) = 0;
};

interface IXAPOParameters : public IUnknown
{
    virtual void SetParameters(const void *pParameters, UINT32 ParametersByteSize) = 0;
    virtual void GetParameters(void *pParameters, UINT32 ParametersByteSize) = 0;
};

extern const GUID IID_IXAPO;
extern const GUID IID_IXAPOParameters;

class CXAPOBase : public IXAPO
{
public:
    CXAPOBase(const XAPO_REGISTRATION_PROPERTIES *pRegistrationProperties)
        : m_pRegistrationProperties(pRegistrationProperties), m_pfnMatrixMixFunction(nullptr),
          m_pfl32MatrixCoefficients(nullptr), m_nSrcFormatType(0), m_fIsLocked(FALSE), m_lReferenceCount(1)
    {
    }
    virtual ~CXAPOBase() {}

    virtual HRESULT QueryInterface(REFIID, void **ppvObject) override
    {
        if (ppvObject)
            *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    virtual ULONG AddRef() override { return ++m_lReferenceCount; }
    virtual ULONG Release() override
    {
        ULONG n = --m_lReferenceCount;
        if (n == 0)
            delete this;
        return n;
    }
    virtual HRESULT GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES **) override { return E_NOTIMPL; }
    virtual HRESULT IsInputFormatSupported(const tWAVEFORMATEX *, const tWAVEFORMATEX *, tWAVEFORMATEX **) override { return S_OK; }
    virtual HRESULT IsOutputFormatSupported(const tWAVEFORMATEX *, const tWAVEFORMATEX *, tWAVEFORMATEX **) override { return S_OK; }
    virtual HRESULT Initialize(const void *, UINT32) override { return S_OK; }
    virtual void Reset() override {}
    virtual HRESULT LockForProcess(UINT32, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *, UINT32, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *) override { m_fIsLocked = TRUE; return S_OK; }
    virtual void UnlockForProcess() override { m_fIsLocked = FALSE; }
    virtual UINT32 CalcInputFrames(UINT32 OutputFrameCount) override { return OutputFrameCount; }
    virtual UINT32 CalcOutputFrames(UINT32 InputFrameCount) override { return InputFrameCount; }

protected:
    const XAPO_REGISTRATION_PROPERTIES *m_pRegistrationProperties;
    void *m_pfnMatrixMixFunction;
    float *m_pfl32MatrixCoefficients;
    UINT32 m_nSrcFormatType;
    BOOL m_fIsLocked;
    volatile LONG m_lReferenceCount;
};

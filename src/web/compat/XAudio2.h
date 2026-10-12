// XAudio2 subset for the Web build: only the types the engine's sound headers
// (snd_driver_xaudio2.h, snd_driver_xaudio2_dsp.h) embed in their structs. There is no
// audio output on the web build yet; src/web/stubs provides null SD_* driver functions.
// The real implementation will be an AudioWorklet back end in a later milestone.
#pragma once
typedef unsigned int XAUDIO2_WINDOWS_PROCESSOR_SPECIFIER;
#include <windows.h>
#include <XAPOBase.h>

#define XAUDIO2_COMMIT_NOW 0
#define XAUDIO2_COMMIT_ALL 0
#define XAUDIO2_NO_LOOP_REGION 0
#define XAUDIO2_LOOP_INFINITE 255
#define XAUDIO2_DEFAULT_CHANNELS 0
#define XAUDIO2_DEFAULT_SAMPLERATE 0
#define XAUDIO2_DEFAULT_FREQ_RATIO 2.0f
#define XAUDIO2_DEFAULT_PROCESSOR 0xffffffff
#define XAUDIO2_END_OF_STREAM 0x0040
#define XAUDIO2_E_INVALID_CALL ((HRESULT)0x88960001)
#define XAUDIO2_E_XAPO_CREATION_FAILED ((HRESULT)0x88960003)
#define XAUDIO2_E_DEVICE_INVALIDATED ((HRESULT)0x88960004)
#define XAUDIO2_MAX_BUFFER_BYTES 0x80000000
#define XAUDIO2_MAX_AUDIO_CHANNELS 64
#define XAUDIO2_MIN_SAMPLE_RATE 1000
#define XAUDIO2_MAX_SAMPLE_RATE 200000
#define XAUDIO2_DEBUG_ENGINE 0x0001
#define XAUDIO2_VOICE_NOPITCH 0x0002
#define XAUDIO2_VOICE_NOSRC 0x0004
#define XAUDIO2_VOICE_USEFILTER 0x0008
#define XAUDIO2_VOICE_MUSIC 0x0010
#define XAUDIO2_VOICE_NOSAMPLESPLAYED 0x0100
#define XAUDIO2_DEFAULT_FILTER_FREQUENCY 1.0f
#define XAUDIO2_DEFAULT_FILTER_ONEOVERQ 1.0f
#define XAUDIO2_MAX_FILTER_FREQUENCY 1.0f
#define XAUDIO2_MAX_FILTER_ONEOVERQ 1.5f
#define XAUDIO2_MAX_VOLUME_LEVEL 16777216.0f
#define XAUDIO2_MAX_FREQ_RATIO 1024.0f
#define XAUDIO2_MAX_LOOP_COUNT 254
#define XAUDIO2_MAX_QUEUED_BUFFERS 64
#define XAUDIO2_PLAY_TAILS 0x0020

#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_ADPCM 2
#define WAVE_FORMAT_IEEE_FLOAT 3
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;

typedef struct adpcmcoef_tag {
    short iCoef1;
    short iCoef2;
} ADPCMCOEFSET;

typedef struct adpcmwaveformat_tag {
    WAVEFORMATEX wfx;
    WORD wSamplesPerBlock;
    WORD wNumCoef;
    ADPCMCOEFSET aCoef[1];
} ADPCMWAVEFORMAT;

typedef struct {
    WAVEFORMATEX Format;
    union {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;
#pragma pack(pop)

interface IXAudio2;
interface IXAudio2Voice;
interface IXAudio2SourceVoice;
interface IXAudio2SubmixVoice;
interface IXAudio2MasteringVoice;

typedef struct XAUDIO2_DEVICE_DETAILS {
    WCHAR DeviceID[256];
    WCHAR DisplayName[256];
    DWORD Role;
    WAVEFORMATEXTENSIBLE OutputFormat;
} XAUDIO2_DEVICE_DETAILS;

typedef struct XAUDIO2_VOICE_DETAILS {
    UINT32 CreationFlags;
    UINT32 InputChannels;
    UINT32 InputSampleRate;
} XAUDIO2_VOICE_DETAILS;

typedef struct XAUDIO2_BUFFER {
    UINT32 Flags;
    UINT32 AudioBytes;
    const BYTE *pAudioData;
    UINT32 PlayBegin;
    UINT32 PlayLength;
    UINT32 LoopBegin;
    UINT32 LoopLength;
    UINT32 LoopCount;
    void *pContext;
} XAUDIO2_BUFFER;

typedef struct XAUDIO2_PERFORMANCE_DATA {
    UINT64 AudioCyclesSinceLastQuery;
    UINT64 TotalCyclesSinceLastQuery;
    UINT32 MinimumCyclesPerQuantum;
    UINT32 MaximumCyclesPerQuantum;
    UINT32 MemoryUsageInBytes;
    UINT32 CurrentLatencyInSamples;
    UINT32 GlitchesSinceEngineStarted;
    UINT32 ActiveSourceVoiceCount;
    UINT32 TotalSourceVoiceCount;
    UINT32 ActiveSubmixVoiceCount;
    UINT32 ActiveResamplerCount;
    UINT32 ActiveMatrixMixCount;
    UINT32 ActiveXmaSourceVoices;
    UINT32 ActiveXmaStreams;
} XAUDIO2_PERFORMANCE_DATA;

typedef struct XAUDIO2_VOICE_STATE {
    void *pCurrentBufferContext;
    UINT32 BuffersQueued;
    UINT64 SamplesPlayed;
} XAUDIO2_VOICE_STATE;

typedef struct XAUDIO2_SEND_DESCRIPTOR {
    UINT32 Flags;
    IXAudio2Voice *pOutputVoice;
} XAUDIO2_SEND_DESCRIPTOR;

typedef struct XAUDIO2_VOICE_SENDS {
    UINT32 SendCount;
    XAUDIO2_SEND_DESCRIPTOR *pSends;
} XAUDIO2_VOICE_SENDS;

typedef struct XAUDIO2_EFFECT_DESCRIPTOR {
    IUnknown *pEffect;
    BOOL InitialState;
    UINT32 OutputChannels;
} XAUDIO2_EFFECT_DESCRIPTOR;

typedef struct XAUDIO2_EFFECT_CHAIN {
    UINT32 EffectCount;
    XAUDIO2_EFFECT_DESCRIPTOR *pEffectDescriptors;
} XAUDIO2_EFFECT_CHAIN;

typedef struct XAUDIO2_FILTER_PARAMETERS {
    int Type;
    float Frequency;
    float OneOverQ;
} XAUDIO2_FILTER_PARAMETERS;

typedef struct XAUDIO2_DEBUG_CONFIGURATION {
    UINT32 TraceMask;
    UINT32 BreakMask;
    BOOL LogThreadID;
    BOOL LogFileline;
    BOOL LogFunctionName;
    BOOL LogTiming;
} XAUDIO2_DEBUG_CONFIGURATION;

interface IXAudio2VoiceCallback
{
    virtual void OnVoiceProcessingPassStart(UINT32 BytesRequired) = 0;
    virtual void OnVoiceProcessingPassEnd() = 0;
    virtual void OnStreamEnd() = 0;
    virtual void OnBufferStart(void *pBufferContext) = 0;
    virtual void OnBufferEnd(void *pBufferContext) = 0;
    virtual void OnLoopEnd(void *pBufferContext) = 0;
    virtual void OnVoiceError(void *pBufferContext, HRESULT Error) = 0;
};

interface IXAudio2Voice
{
    virtual void GetVoiceDetails(XAUDIO2_VOICE_DETAILS *pVoiceDetails) = 0;
    virtual HRESULT SetOutputVoices(const XAUDIO2_VOICE_SENDS *pSendList) = 0;
    virtual HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN *pEffectChain) = 0;
    virtual HRESULT EnableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) = 0;
    virtual HRESULT DisableEffect(UINT32 EffectIndex, UINT32 OperationSet = 0) = 0;
    virtual void GetEffectState(UINT32 EffectIndex, BOOL *pEnabled) = 0;
    virtual HRESULT SetEffectParameters(UINT32 EffectIndex, const void *pParameters, UINT32 ParametersByteSize, UINT32 OperationSet = 0) = 0;
    virtual HRESULT GetEffectParameters(UINT32 EffectIndex, void *pParameters, UINT32 ParametersByteSize) = 0;
    virtual HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS *pParameters, UINT32 OperationSet = 0) = 0;
    virtual void GetFilterParameters(XAUDIO2_FILTER_PARAMETERS *pParameters) = 0;
    virtual HRESULT SetVolume(float Volume, UINT32 OperationSet = 0) = 0;
    virtual void GetVolume(float *pVolume) = 0;
    virtual HRESULT SetChannelVolumes(UINT32 Channels, const float *pVolumes, UINT32 OperationSet = 0) = 0;
    virtual void GetChannelVolumes(UINT32 Channels, float *pVolumes) = 0;
    virtual HRESULT SetOutputMatrix(IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, const float *pLevelMatrix, UINT32 OperationSet = 0) = 0;
    virtual void GetOutputMatrix(IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, float *pLevelMatrix) = 0;
    virtual void DestroyVoice() = 0;
};

interface IXAudio2SourceVoice : public IXAudio2Voice
{
    virtual HRESULT Start(UINT32 Flags = 0, UINT32 OperationSet = 0) = 0;
    virtual HRESULT Stop(UINT32 Flags = 0, UINT32 OperationSet = 0) = 0;
    virtual HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER *pBuffer, const void *pBufferWMA = nullptr) = 0;
    virtual HRESULT FlushSourceBuffers() = 0;
    virtual HRESULT Discontinuity() = 0;
    virtual HRESULT ExitLoop(UINT32 OperationSet = 0) = 0;
    virtual void GetState(XAUDIO2_VOICE_STATE *pVoiceState) = 0;
    virtual HRESULT SetFrequencyRatio(float Ratio, UINT32 OperationSet = 0) = 0;
    virtual void GetFrequencyRatio(float *pRatio) = 0;
    virtual HRESULT SetSourceSampleRate(UINT32 NewSourceSampleRate) = 0;
};

interface IXAudio2SubmixVoice : public IXAudio2Voice
{
};

interface IXAudio2MasteringVoice : public IXAudio2Voice
{
};

interface IXAudio2 : public IUnknown
{
    virtual HRESULT GetDeviceCount(UINT32 *pCount) = 0;
    virtual HRESULT GetDeviceDetails(UINT32 Index, XAUDIO2_DEVICE_DETAILS *pDeviceDetails) = 0;
    virtual HRESULT Initialize(UINT32 Flags = 0, UINT32 XAudio2Processor = XAUDIO2_DEFAULT_PROCESSOR) = 0;
    virtual HRESULT RegisterForCallbacks(void *pCallback) = 0;
    virtual void UnregisterForCallbacks(void *pCallback) = 0;
    virtual HRESULT CreateSourceVoice(IXAudio2SourceVoice **ppSourceVoice, const WAVEFORMATEX *pSourceFormat, UINT32 Flags = 0, float MaxFrequencyRatio = XAUDIO2_DEFAULT_FREQ_RATIO, IXAudio2VoiceCallback *pCallback = nullptr, const XAUDIO2_VOICE_SENDS *pSendList = nullptr, const XAUDIO2_EFFECT_CHAIN *pEffectChain = nullptr) = 0;
    virtual HRESULT CreateSubmixVoice(IXAudio2SubmixVoice **ppSubmixVoice, UINT32 InputChannels, UINT32 InputSampleRate, UINT32 Flags = 0, UINT32 ProcessingStage = 0, const XAUDIO2_VOICE_SENDS *pSendList = nullptr, const XAUDIO2_EFFECT_CHAIN *pEffectChain = nullptr) = 0;
    virtual HRESULT CreateMasteringVoice(IXAudio2MasteringVoice **ppMasteringVoice, UINT32 InputChannels = XAUDIO2_DEFAULT_CHANNELS, UINT32 InputSampleRate = XAUDIO2_DEFAULT_SAMPLERATE, UINT32 Flags = 0, UINT32 DeviceIndex = 0, const XAUDIO2_EFFECT_CHAIN *pEffectChain = nullptr) = 0;
    virtual HRESULT StartEngine() = 0;
    virtual void StopEngine() = 0;
    virtual HRESULT CommitChanges(UINT32 OperationSet) = 0;
    virtual void GetPerformanceData(XAUDIO2_PERFORMANCE_DATA *pPerfData) = 0;
    virtual void SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION *pDebugConfiguration, void *pReserved = nullptr) = 0;
};

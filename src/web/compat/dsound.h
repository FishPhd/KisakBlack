// DirectSound types for the web build. Only the declarations r_cinematic.cpp and
// groupvoice/directsound.h name; there is no DirectSound device (CreateX returns failure).
#pragma once
#include <windows.h>
interface IDirectSound8 : public IUnknown
{
    virtual HRESULT SetCooperativeLevel(HWND, DWORD) = 0;
};
interface IDirectSoundBuffer : public IUnknown {};
interface IDirectSoundCaptureBuffer : public IUnknown {};
typedef IDirectSound8 *LPDIRECTSOUND8;
typedef IDirectSound8 *LPDIRECTSOUND;
typedef IDirectSoundBuffer *LPDIRECTSOUNDBUFFER;
typedef IDirectSoundCaptureBuffer *LPDIRECTSOUNDCAPTUREBUFFER;
inline HRESULT DirectSoundCreate8(const GUID *, LPDIRECTSOUND8 *out, IUnknown *) { if (out) *out = nullptr; return E_FAIL; }

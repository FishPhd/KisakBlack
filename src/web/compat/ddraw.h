// DirectDraw: only what r_texturemem.cpp names. The factory function pointers are looked up
// with GetProcAddress at runtime and are null on the web build, so no IDirectDraw7 is ever created.
#pragma once
#include <windows.h>
typedef struct _DDSCAPS2 {
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    union { DWORD dwCaps4; DWORD dwVolumeDepth; };
} DDSCAPS2, *LPDDSCAPS2;
#define DDSCAPS_TEXTURE 0x00001000
#define DDSCAPS_VIDEOMEMORY 0x00004000
#define DDSCAPS_LOCALVIDMEM 0x10000000
interface IDirectDraw7 : public IUnknown
{
    virtual HRESULT SetCooperativeLevel(HWND, DWORD) = 0;
    virtual HRESULT GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps2, LPDWORD lpdwTotal, LPDWORD lpdwFree) = 0;
};
extern const GUID IID_IDirectDraw7;

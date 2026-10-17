// Inert implementations of the platform services the web (headless dedicated server) build does
// not have: voice chat, Steam, the XAudio2 driver, NVIDIA stereo and the developer
// "monkey" harness. Each one matches the behaviour of the desktop code when the feature is absent.

#include <win32/win_gamepad.h>
#include <win32/win_voice.h>
#include <win32/win_steam.h>
#include <win32/win_main.h>
#include <sound/snd_driver_xaudio2.h>
#include <monkey/monkey.h>
#include <live/live_offline.h>
#include <physics/phys_broad_phase.h>
#include <CubeMapGenLib/CCubeMapProcessor.h>
#include <CubeMapGenLib/CImageSurface.h>
#include <nvapi/nvapi.h>
#include <win32/win_splash.h>
#include <win32/win_wndproc.h>
#include <vpx/vpx.h>
#include <ddraw.h>

// ---- Voice ------------------------------------------------------------------------------------

float voice_current_scaler = 1.0f;

bool __cdecl Voice_Init() { return false; }
void __cdecl Voice_StopClientSamples() {}
void __cdecl Voice_Shutdown() {}
double __cdecl Voice_GetVoiceLevel() { return 0.0; }
void __cdecl Voice_Playback() {}
int __cdecl Voice_GetLocalVoiceData() { return 0; }
void __cdecl Voice_IncomingVoiceData(unsigned __int8, unsigned __int8 *, int) {}
bool __cdecl Voice_IsClientTalking(unsigned int) { return false; }
char __cdecl Voice_StartRecording() { return 0; }
char __cdecl Voice_StopRecording() { return 0; }
int __cdecl Live_GetClientNumForXuid(const SessionData_s *, unsigned __int64) { return -1; }

// ---- Steam (same inert behaviour as win_steam.cpp) ---------------------------------------------

bool g_steamInitialized = false;

void Steam_Init() {}
void Steam_Shutdown() {}
bool Steam_UpdateClientAuthTicket(netadr_t) { return true; }

bool Steam_GetRawClientTicket(unsigned char **pBuffer, uint32 *pSize)
{
    if (pBuffer)
        *pBuffer = nullptr;
    if (pSize)
        *pSize = 0;
    return false;
}

void Steam_CancelClientTicket() {}
uint64_t Steam_GetClientSteamID64() { return (uint64_t)LiveOffline_GetXuid(); }
bool Steam_CheckClientTicket(const void *, uint32, uint64_t) { return false; }
void Steam_CheckClients() {}
void Steam_OnClientDropped(uint64_t) {}
void Steam_SV_AddTestCommands() {}

// ---- Sound driver (no XAudio2: the dedicated server never starts a voice) ---------------------

// SD_* and SND_IsVoiceFree live in web_audio.cpp (Web Audio). Device-name truncation stays here.
void __cdecl SD_TruncateAudioDeviceNames(Font_s *, float, int) {}

// ---- Monkey (developer test harness) ----------------------------------------------------------

void __cdecl Monkey_ComPrintHook(const char *) {}
void __cdecl Monkey_Error(const char *) {}
void __cdecl Monkey_GrabComPrints(bool) {}
bool __cdecl Monkey_IsRunning() { return false; }
void __cdecl Monkey_KeepAlive() {}
bool __cdecl Monkey_UseRandomInput() { return false; }

// ---- Physics: the base class of the terrain-query callback has no body in the decompile --------
// MSVC never needed one (the vtable is emitted where the derived object is constructed); the
// Itanium ABI emits the base vtable and typeinfo where the first virtual function is defined.

void broad_phase_terrain_query_callback::query(const broad_phase_environment_query_input *,
                                               broad_phase_environement_query_results *)
{
}

// ---- Cube map filtering (screenshot-only tool) -------------------------------------------------

CCubeMapProcessor::CCubeMapProcessor() {}
CCubeMapProcessor::~CCubeMapProcessor() {}
void CCubeMapProcessor::Init(int32, int32, int32, int32) {}
void CCubeMapProcessor::SetInputFaceData(int32, int32, int32, int32, void *, float32, float32, float32) {}
void CCubeMapProcessor::GetOutputFaceData(int32, int32, int32, int32, int32, void *, float32, float32) {}
void CCubeMapProcessor::InitiateFiltering(float32, float32, float32, int32, int32, int32, bool8) {}
int32 CCubeMapProcessor::GetStatus(void) { return 0; }

CImageSurface::CImageSurface() {}
CImageSurface::~CImageSurface() {}

// ---- Monkey, continued -------------------------------------------------------------------------

void __cdecl Monkey_Start() {}
void __cdecl Monkey_Frame() {}
void __cdecl Monkey_Event(const char *) {}

// ---- NVIDIA stereo (no driver library: Initialize fails, so the renderer never asks again) ------

NvAPI_Status __cdecl NvAPI_Initialize() { return NVAPI_LIBRARY_NOT_FOUND; }
NvAPI_Status __cdecl NvAPI_Stereo_CreateHandleFromIUnknown(IUnknown *, StereoHandle *) { return NVAPI_NO_IMPLEMENTATION; }
NvAPI_Status __cdecl NvAPI_Stereo_DestroyHandle(StereoHandle) { return NVAPI_NO_IMPLEMENTATION; }
NvAPI_Status __cdecl NvAPI_Stereo_IsActivated(StereoHandle, NvU8 *) { return NVAPI_NO_IMPLEMENTATION; }
NvAPI_Status __cdecl NvAPI_Stereo_SetConvergence(StereoHandle, float) { return NVAPI_NO_IMPLEMENTATION; }

// ---- Sound driver lifecycle --------------------------------------------------------------------

// ---- Splash window / hotkeys / VP8 video -------------------------------------------------------

void __cdecl Sys_DestroySplashWindow() {}
void __cdecl Sys_HideSplashWindow() {}
void __cdecl Sys_UpdateHotkeyBlock() {}
void __cdecl vpx_shutdown() {}

// ---- DirectDraw (texture memory probe; the loader never resolves, so this is only an address) ---

const GUID IID_IDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};

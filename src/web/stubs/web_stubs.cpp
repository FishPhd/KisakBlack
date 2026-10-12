// Inert implementations of the platform services the web (headless dedicated server) build does
// not have: gamepads, voice chat, Steam, the XAudio2 driver, NVIDIA stereo and the developer
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

// ---- Gamepad ----------------------------------------------------------------------------------

const dvar_t *gpad_debug;
const dvar_t *gpad_button_lstick_deflect_max;
const dvar_t *gpad_button_rstick_deflect_max;
const dvar_t *gpad_button_deadzone;
const dvar_t *gpad_stick_deadzone_min;
const dvar_t *gpad_stick_deadzone_max;
const dvar_t *gpad_stick_pressed;
const dvar_t *gpad_stick_pressed_hysteresis;
const dvar_t *gpad_rumble;
const dvar_t *gpad_menu_scroll_delay_first;
const dvar_t *gpad_menu_scroll_delay_rest;
const dvar_t *gpad_buttonsConfig;
const dvar_t *gpad_sticksConfig;
const dvar_t *gpad_enabled;
const dvar_t *gpad_present;

// The profile code reads and writes these dvars, so they must exist (same names/defaults as win_gamepad.cpp).
void GPad_InitAll()
{
    gpad_debug = _Dvar_RegisterInt("gpad_debug", 0, 0x80000000, 0x7FFFFFFF, 0, "coder use only");
    gpad_button_lstick_deflect_max = _Dvar_RegisterFloat("gpad_button_lstick_deflect_max", 1.0f, 0.0f, 1.0f, 0, "Game pad maximum pad stick pressed value");
    gpad_button_rstick_deflect_max = _Dvar_RegisterFloat("gpad_button_rstick_deflect_max", 1.0f, 0.0f, 1.0f, 0, "Game pad maximum pad stick pressed value");
    gpad_button_deadzone = _Dvar_RegisterFloat("gpad_button_deadzone", 0.13f, 0.0f, 1.0f, 0x80u, "Game pad button deadzone threshhold");
    gpad_stick_deadzone_min = _Dvar_RegisterFloat("gpad_stick_deadzone_min", 0.2f, 0.0f, 1.0f, 0x80u, "Game pad minimum stick deadzone");
    gpad_stick_deadzone_max = _Dvar_RegisterFloat("gpad_stick_deadzone_max", 0.01f, 0.0f, 1.0f, 0x80u, "Game pad maximum stick deadzone");
    gpad_stick_pressed = _Dvar_RegisterFloat("gpad_stick_pressed", 0.4f, 0.0f, 1.0f, 0x80u, "Game pad stick pressed threshhold");
    gpad_stick_pressed_hysteresis = _Dvar_RegisterFloat("gpad_stick_pressed_hysteresis", 0.1f, 0.0f, 1.0f, 0x80u, "Game pad stick pressed no-change-zone around gpad_stick_pressed to prevent bouncing");
    gpad_rumble = _Dvar_RegisterBool("gpad_rumble", 1, 1u, "Enable game pad rumble");
    gpad_menu_scroll_delay_first = _Dvar_RegisterInt("gpad_menu_scroll_delay_first", 420, 0, 1000, 1u, "Menu scroll key-repeat delay, for the first repeat, in milliseconds");
    gpad_menu_scroll_delay_rest = _Dvar_RegisterInt("gpad_menu_scroll_delay_rest", 210, 0, 1000, 1u, "Menu scroll key-repeat delay, for repeats after the first, in milliseconds");
    gpad_buttonsConfig = _Dvar_RegisterString("gpad_buttonsConfig", "buttons_default", 1u, "Game pad button configuration");
    gpad_sticksConfig = _Dvar_RegisterString("gpad_sticksConfig", "thumbstick_default", 1u, "Game pad stick configuration");
    gpad_enabled = _Dvar_RegisterBool("gpad_enabled", 0, 1u, "Game pad enabled");
    gpad_present = _Dvar_RegisterBool("gpad_present", 0, 0x40u, "Game pad present");
}
void __cdecl GPad_UpdateAll() {}
int GPad_RefreshAll() { return 0; }
void __cdecl GPad_Check(int) {}
void __cdecl GPad_ResetState(int) {}
bool __cdecl GPad_IsActive(int) { return false; }
double __cdecl GPad_GetButton(int, GamePadButton) { return 0.0; }
bool __cdecl GPad_ButtonRequiresUpdates(int, GamePadButton) { return false; }
bool __cdecl GPad_IsButtonPressed(int, GamePadButton) { return false; }
bool __cdecl GPad_IsButtonReleased(int, GamePadButton) { return false; }
double __cdecl GPad_GetStick(int, GamePadStick) { return 0.0; }
bool __cdecl GPad_IsStickPressed(int, GamePadStick, GamePadStickDir) { return false; }
bool __cdecl GPad_GetStickChangedToPressedState(int, GamePadStick, GamePadStickDir, bool) { return false; }
bool __cdecl GPad_IsStickReleased(int, GamePadStick, GamePadStickDir) { return false; }

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

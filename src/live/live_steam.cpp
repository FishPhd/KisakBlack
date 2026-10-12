#include "live_steam.h"
#include <win32/win_splash.h>
#include <qcommon/common.h>
#include <win32/win_main.h>

const dvar_t *livesteam_sv_vac;

// ---------------------------------------------------------------------------
// Offline build: all LiveSteam_* entry points are inert stubs so that the
// remaining callers (live_win, DW, sv_main, cl_main, ...) link and never crash.
// ---------------------------------------------------------------------------

void __cdecl SteamAPIDebugTextHook(int severity, const char *pchDebugText)
{
}

void __cdecl LiveSteam_Quit(bool showErrorMessage)
{
    Sys_HideSplashWindow();
    Sys_Quit();
}

void __cdecl LiveSteam_CheckAccess()
{
}

bool __cdecl LiveSteam_IsInitialized()
{
    return false;
}

char __cdecl LiveSteam_IsClientSignedInLocally()
{
    return 0;
}

bool __cdecl LiveSteam_IsClientSignedInOnline()
{
    return false;
}

unsigned __int64 __cdecl LiveSteam_GetClientIDAsXUID()
{
    return 0;
}

const char *__cdecl LiveSteam_GetCountryCode()
{
    const char *loc = Dvar_GetString("cl_forceloc");
    if (loc && *loc)
        return loc;

    return "";
}

unsigned __int64 __cdecl LiveSteam_GetUid()
{
    return 0;
}

unsigned int __stdcall ShellExecuteThredProc(const char *lpParam)
{
    return 0;
}

bool __cdecl LiveSteam_LaunchOtherApp(const char *cmd)
{
    return false;
}

void __cdecl LiveSteam_ShowStore()
{
}

void __cdecl LiveSteam_PopOverlayForSteamID(unsigned __int64 steamID)
{
}

void __cdecl LiveSteam_Init()
{
    // Keep the dvar registered (sv_main_mp reads it for the heartbeat/info string) but never enable VAC.
    livesteam_sv_vac = _Dvar_RegisterBool("sv_vac", 0, 1u, "Enable VAC on this server");
}

void __cdecl LiveSteam_TestFriends_f()
{
}

FILE *LiveSteam_CreateSteamAppIdFile()
{
    return NULL;
}

void __cdecl LiveSteam_Shutdown()
{
}

void __cdecl LiveSteam_CheckForP2PMessages()
{
}

void __cdecl LiveSteam_SendP2PMessage(unsigned __int64 uid, unsigned __int8 *payload, unsigned int payloadsize)
{
}

void __cdecl LiveSteam_Frame()
{
}

void __cdecl LiveSteam_RunCallbacks()
{
}

const char *__cdecl LiveSteam_GetClientPersonaName(bool shortName)
{
    return "";
}

char __cdecl LiveSteam_DWUserNameFromSteamID(char *dwUserName)
{
    if (dwUserName)
        *dwUserName = 0;
    return 0;
}

char __cdecl LiveSteam_AuthRequestTicket(const void *authBlob, unsigned int authBlobSize)
{
    return 0;
}

char __cdecl LiveSteam_AuthGetRequestedTicket(void *ticketBuf, unsigned int ticketBufSize, unsigned int *ticketSize)
{
    if (ticketSize)
        *ticketSize = 0;
    return 0;
}

int __cdecl LiveSteam_Client_ConnectToSteamServer(unsigned __int64 serverID, void *authBlob, unsigned int bufferSize)
{
    return 0;
}

void LiveSteam_Client_SteamDisconnect()
{
}

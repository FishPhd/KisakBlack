#include "live_steam_server.h"

// Offline build: inert stubs (no Steam game server, no callbacks).

void __cdecl LiveSteam_Server_Init()
{
}

void __cdecl LiveSteam_Server_Shutdown()
{
}

void __cdecl LiveSteam_Server_Frame()
{
}

void __cdecl LiveSteam_Server_RunCallbacks()
{
}

void __cdecl LiveSteam_Server_ClientSteamAuthentication(
                unsigned __int64 clientSteamID,
                netadr_t netAddr,
                void *authBlob,
                unsigned int authBlobSize)
{
}

void __cdecl LiveSteam_Server_ClientSteamDisconnect(unsigned __int64 clientSteamID)
{
}

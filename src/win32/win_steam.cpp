// LWSS: this is a Rudimentary Steam Authentication for the Client -> Dedicated Server
// The Client Generates a Steam ticket and sends it to the Server (in lieu of a CD Key)
// The Server checks this Ticket and then puts in a delayed networked check to Steam.

// You might be wondering, why did I not just use the Black Ops existing Steam implementation. 
// Well that's because it's overcomplicated.

#include "win_steam.h"

#include <live/live_offline.h>
#include <qcommon/net_chan_mp.h>

// ---------------------------------------------------------------------------
// Offline build: every entry point is an inert stub. Nothing here touches
// the Steam client, creates steam_appid.txt, or sets Steam environment variables.
// ---------------------------------------------------------------------------

bool g_steamInitialized = false;

void Steam_Init()
{
}

void Steam_Shutdown()
{
}

bool Steam_UpdateClientAuthTicket(netadr_t serverIpv4)
{
	// Called via CL_CDKeyValidate() before a non-loopback connect. There is no ticket to obtain in a
	// no-Steam build and the no-Steam server accepts ticketless clients, so report success.
	return true;
}

bool Steam_GetRawClientTicket(unsigned char **pBuffer, uint32 *pSize)
{
	// No Steam ticket exists in this build. Callers must handle "false".
	if (pBuffer)
		*pBuffer = NULL;
	if (pSize)
		*pSize = 0;
	return false;
}

void Steam_CancelClientTicket()
{
}

uint64_t Steam_GetClientSteamID64()
{
	// Offline identity: the client sends its local profile XUID in "getchallenge".
	return (uint64_t)LiveOffline_GetXuid();
}

bool Steam_CheckClientTicket(const void *pAuthTicket, uint32 authTicketLen, uint64_t steamID64)
{
	return false;
}

void Steam_CheckClients()
{
}

void Steam_OnClientDropped(uint64_t steamID64)
{
}

void Steam_SV_AddTestCommands()
{
}

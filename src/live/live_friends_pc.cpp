#include "live_friends_pc.h"

#include "live_meetplayer.h"

int g_sortedNumFriends;
int g_sortedFriendsRedirect[256];

bool g_sortedFriendsListDirty = true;

// Offline build: there is no friends list. Every query reports "nobody".

int __cdecl LiveSteam_GetNumFriends()
{
    return 0;
}

int __cdecl LiveSteam_GetNumFriendsOnTheServer()
{
    return 0;
}

unsigned __int64 __cdecl LiveSteam_GetFriendXuid(int index)
{
    return 0;
}

void __cdecl LiveSteam_GetFriendPersonaName(int index, char *buf, int size)
{
    if (buf && size > 0)
        *buf = 0;
}

void __cdecl LiveSteam_GetFriendOnTheServerPersonaName(int index, char *buf, int size)
{
    if (buf && size > 0)
        *buf = 0;
}

int __cdecl LiveSteam_GetFriendPersonaState(unsigned __int64 xuid)
{
    return LIVE_STEAM_PERSONA_STATE_OFFLINE;
}

int __cdecl LiveSteam_GetFriendPersonaStateByIndex(int index)
{
    return LIVE_STEAM_PERSONA_STATE_OFFLINE;
}

char __cdecl Friends_Update()
{
    g_sortedFriendsListDirty = 1;
    return 1;
}

int __cdecl Friends_GetCount(int __formal, int type)
{
    if ( type == 1 )
        return LiveMeetPlayer_GetNoOfMetPlayers(0);
    return 0;
}

char __cdecl Friends_GetByIndex(int controllerIndex, int filter, int index, FriendInfo *info)
{
    return 0;
}

char __cdecl Friends_GetByID(int controllerIndex, unsigned __int64 id, FriendInfo *info)
{
    return 0;
}

bool __cdecl Friend_GetByName(int controllerIndex, const char *friendName, unsigned __int64 *uid)
{
    return false;
}

void __cdecl Friends_GetPeopleOnServer()
{
}

void __cdecl Friend_PrintNamesOnServer_f()
{
    Friends_GetPeopleOnServer();
}

cmd_function_s Friend_PrintNamesOnServer_f_VAR;
void __cdecl Friends_Init()
{
    Cmd_AddCommandInternal("getpeopleonserver", Friend_PrintNamesOnServer_f, &Friend_PrintNamesOnServer_f_VAR);
    g_sortedFriendsListDirty = 1;
}

void __cdecl Live_GetRecentPlayerName(unsigned __int64 uid, char *buf, int bufsize)
{
    if (buf && bufsize > 0)
        *buf = 0;
}

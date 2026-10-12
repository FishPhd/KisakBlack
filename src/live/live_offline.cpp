// Offline (no Steam / no DemonWare) local identity and sign-in.
//
// When KISAK_LIVE is not defined every online service is compiled out. This module provides a
// local "profile" (random 64-bit XUID persisted in players/kisak_profile.dat plus the `name` dvar as
// gamertag) and flips the same state the original sign-in flow would have set, so that menus gated on
// IsSignedIn / IsSignedInToLive / AreStatsFetched / IsDemonwareFetchingDone pass.

#include "live_offline.h"

#include "live_win.h"
#include "live_storage.h"
#include "live_storage_pub.h"
#include "live_storage_win.h"
#include "live_stats.h"

#include <qcommon/com_clients.h>
#include <client/client.h>
#include <universal/com_files.h>
#include <qcommon/net_chan_mp.h>
#include <win32/win_gamerprofile.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <intrin.h>

extern XenonUserData xenonUserData[1];

// storage state that has no setter; the globals themselves are not static
extern bool s_haveContracts; // live_storage_pub.cpp
extern bool s_UTCSynced;     // live_storage.cpp

static bool s_offlineSignedIn;
static bool s_offlineStatsReady;
static unsigned __int64 s_offlineXuid;
static char s_offlineGamertag[32];

static unsigned __int64 LiveOffline_Mix64(unsigned __int64 z)
{
    // splitmix64 finalizer
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static unsigned __int64 LiveOffline_GenerateXuid()
{
    unsigned __int64 seed = (unsigned __int64)__rdtsc();
    seed ^= (unsigned __int64)time(NULL) << 20;
    seed ^= (unsigned __int64)(size_t)&seed;
    unsigned __int64 xuid = LiveOffline_Mix64(seed);

    // keep it positive as a signed 64-bit (menus print it with %lld) and well away from small ids
    xuid &= 0x7FFFFFFFFFFFFFFFULL;
    xuid |= 0x0100000000000000ULL;
    return xuid;
}

static void LiveOffline_GetProfilePath(char *path, size_t pathSize, bool makeDir)
{
    const char *home = ".";
    if (fs_homepath && fs_homepath->current.string && fs_homepath->current.string[0])
        home = fs_homepath->current.string;

    if (makeDir)
    {
        char dir[512];
#ifdef KISAK_WEB
        _snprintf(dir, sizeof(dir), "%s/players", home);
#else
        _snprintf(dir, sizeof(dir), "%s\\players", home);
#endif
        dir[sizeof(dir) - 1] = 0;
        _mkdir(dir); // fails harmlessly with EEXIST
    }

#ifdef KISAK_WEB
    _snprintf(path, pathSize, "%s/players/kisak_profile.dat", home);
#else
    _snprintf(path, pathSize, "%s\\players\\kisak_profile.dat", home);
#endif
    path[pathSize - 1] = 0;
}

static bool LiveOffline_ReadIdentity(unsigned __int64 *xuid)
{
    char path[512];
    LiveOffline_GetProfilePath(path, sizeof(path), false);

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    bool ok = false;
    char line[128];
    char magic[32];
    int version = 0;
    unsigned long long value = 0;
    if (fgets(line, sizeof(line), f)
        && sscanf(line, "%31s %d %llu", magic, &version, &value) == 3
        && !strcmp(magic, "KISAKPROFILE")
        && version == 1
        && value != 0)
    {
        *xuid = value;
        ok = true;
    }
    fclose(f);
    return ok;
}

static void LiveOffline_WriteIdentity(unsigned __int64 xuid)
{
    char path[512];
    LiveOffline_GetProfilePath(path, sizeof(path), true);

    FILE *f = fopen(path, "wb");
    if (!f)
    {
        Com_PrintWarning(16, "LiveOffline: could not write %s (profile id will not persist)\n", path);
        return;
    }
    fprintf(f, "KISAKPROFILE 1 %llu\n", (unsigned long long)xuid);
    fclose(f);
}

static void LiveOffline_EnsureIdentity()
{
    if (!s_offlineXuid)
    {
        unsigned __int64 xuid = 0;
        if (!LiveOffline_ReadIdentity(&xuid))
        {
            xuid = LiveOffline_GenerateXuid();
            LiveOffline_WriteIdentity(xuid);
            Com_Printf(16, "LiveOffline: created new local profile id %llu\n", (unsigned long long)xuid);
        }
        s_offlineXuid = xuid;
    }

    // gamertag follows the `name` dvar (user configurable); fall back to a default
    const char *dvarName = Dvar_GetString("name");
    if (dvarName && dvarName[0])
        I_strncpyz(s_offlineGamertag, dvarName, sizeof(s_offlineGamertag));
    else if (!s_offlineGamertag[0])
        I_strncpyz(s_offlineGamertag, "Player", sizeof(s_offlineGamertag));
}

unsigned long long LiveOffline_GetXuid()
{
    LiveOffline_EnsureIdentity();
    return s_offlineXuid;
}

const char *LiveOffline_GetGamertag()
{
    LiveOffline_EnsureIdentity();
    return s_offlineGamertag;
}

bool LiveOffline_IsSignedIn()
{
    return s_offlineSignedIn;
}

bool LiveOffline_AreStatsReady()
{
    return s_offlineSignedIn && s_offlineStatsReady;
}

static void LiveOffline_MarkServicesReady()
{
    // playlists: read from disk when present, otherwise report "have" with an empty list
    if (!LiveStorage_DoWeHavePlaylists())
    {
        LiveStorage_FetchPlaylistsLocal(0);
        if (!LiveStorage_DoWeHavePlaylists())
            LiveStorage_SetHavePlaylists(true);
    }

    // contracts and time sync have no local source; mark them ready
    s_haveContracts = true;
    s_UTCSynced = true;
}

bool LiveOffline_SignIn(int controllerIndex)
{
    if (controllerIndex != 0)
        return false;

    LiveOffline_EnsureIdentity();

    const bool firstSignIn = !s_offlineSignedIn;

    if (firstSignIn)
    {
        // same sequence as Live_UserSignedIn, minus anything that talks to DemonWare
        LiveStorage_NewUser(controllerIndex);

        xenonUserData[controllerIndex].signinState = 2;
        xenonUserData[controllerIndex].isGuestUser = false;
        xenonUserData[controllerIndex].tier = USER_TIER_NONE;
        xenonUserData[controllerIndex].totalGamesPlayed = 0;
        xenonUserData[controllerIndex].xuid = s_offlineXuid;
        XUIDToString(&xenonUserData[controllerIndex].xuid, xenonUserData[controllerIndex].xuidString);

        GamerProfile_InitAllProfiles();
        GamerProfile_LogInProfile(controllerIndex);

        s_offlineSignedIn = true;
    }

    // keep the displayed gamertag in sync with the `name` dvar
    I_strncpyz(xenonUserData[controllerIndex].gamertag, s_offlineGamertag, sizeof(xenonUserData[controllerIndex].gamertag));

    Dvar_SetBoolByName("xblive_loggedin", true);
    Dvar_SetBoolByName("dw_loggedin", true);
    // Fileshare and the private-lobby menu call SV_GetOwnerID. An empty
    // sv_ownerid printed "Must set ownerid first" before the match started.
    Dvar_SetStringByName("sv_ownerid", xenonUserData[controllerIndex].xuidString);

    if (firstSignIn)
        Com_Printf(16, "LiveOffline: signed in as '%s' (xuid %llu)\n", s_offlineGamertag, (unsigned long long)s_offlineXuid);

    if (!s_offlineStatsReady)
    {
        s_offlineStatsReady = LiveOffline_LoadStats(controllerIndex);
        if (s_offlineStatsReady)
        {
            LiveStorage_SetStatsFetched(controllerIndex, STATS_LOCATION_NORMAL, true);
            LiveStorage_SetStatsFetched(controllerIndex, STATS_LOCATION_FORCE_NORMAL, true);
            LiveStorage_SetStatsFetched(controllerIndex, STATS_LOCATION_BASICTRAINING, true);
            LiveStats_MakeStableStatsBuffer(controllerIndex);
        }
        else
            Com_PrintWarning(16, "LiveOffline: local stats are not ready yet\n");
    }

    LiveOffline_MarkServicesReady();

    return true;
}

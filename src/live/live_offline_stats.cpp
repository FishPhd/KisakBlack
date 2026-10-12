// Offline (no DemonWare / no Steam) local stats persistence.
//
// The original game stores the player's stats in DemonWare (mpstatsCompressed / globalstatsCompressed /
// btstatsCompressed) and uploads / downloads them on demand. This file replaces that with two local files
// (written atomically, temp file + rename):
//
//   players/kisak_mpstats.dat : header + [NORMAL buffer][GLOBAL buffer]   (online ranked / normal profile)
//   players/kisak_btstats.dat : header + [BASICTRAINING buffer]
//
// Every buffer is exactly LiveStorage_GetStatsBufferSize() (40548) bytes of DDL data (mp/stats.ddl); the
// first 4 bytes are the DDL checksum written by LiveStats_WriteChecksumToBuffer. The 4 bytes after the buffer
// inside controllerNetworkData (checksumValid / writeNeeded / DDLvalidated / fetched) are runtime-only and are
// not stored on disk.
//
// The DDL definition comes from a fastfile (ddl_mp/stats.ddl) so LiveOffline_LoadStats can only succeed after
// LiveStats_Init (g_statsDDL) ran; it returns false otherwise and the caller may retry later.

#include "live_offline_stats.h"
#include "live_offline.h"
#include "live_stats.h"
#include "live_storage.h"
#include "live_storage_win.h"
#include "live_win.h"

#include <qcommon/common.h>
#include <qcommon/com_gamemodes.h>
#include <qcommon/net_chan_mp.h>
#include <universal/com_files.h>
#include <ddl/ddl_api.h>
#include <bgame/bg_unlockable_items.h>
#include <ui_mp/ui_gametype_custom_mp.h>
#include <server_mp/sv_main_mp.h>
#include <server/server.h>

#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <map>
#include <algorithm>
#include <vector>
#include <stdlib.h>
#include <client_mp/cl_main_mp.h>
#include <cgame_mp/cg_main_mp.h>
#include <game_mp/g_cmds_mp.h>
#include <win32/win_shared.h>
#include <qcommon/cmd.h>

#ifndef _WINDOWS_
extern "C" __declspec(dllimport) int __stdcall MoveFileExA(const char *lpExistingFileName, const char *lpNewFileName, unsigned long dwFlags);
#endif
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH 0x00000008
#endif

// controllerNetworkData is defined in live_storage.cpp
extern playerNetworkData controllerNetworkData[1];

namespace
{
    const int kStatsSize = 40548;              // == LiveStorage_GetStatsBufferSize()
    const unsigned int kFileMagic = 0x5453424Bu; // 'KBST'
    const unsigned int kFileVersion = 1;

    const char *const kMpStatsFile = "kisak_mpstats.dat";
    const char *const kBtStatsFile = "kisak_btstats.dat";

    struct OfflineStatsFileHeader
    {
        unsigned int magic;
        unsigned int version;
        unsigned int bufferSize;
        unsigned int bufferCount;
    };

    enum RawBuffer
    {
        RAW_NORMAL = 0,
        RAW_BASICTRAINING = 1,
        RAW_GLOBAL = 2,
    };

    bool s_loaded = false;
    bool s_loading = false;

    // file scratch (kept static so we do not put ~160 KB on the stack)
    unsigned char s_fileBuf[3][kStatsSize];
    unsigned char s_ddlBackup[kStatsSize];

    // template for a fresh (version-stamped, empty) stats buffer used for bots / remote clients
    unsigned char s_freshBuffer[kStatsSize];
    bool s_freshBufferValid = false;

    // state of the local client's server-side stats when they were handed to the server
    bool s_serverFilledLocal = false;
    bool s_serverFilledWasBT = false;
    unsigned char s_serverFillSnapshot[kStatsSize];

    unsigned char *RawBuf(RawBuffer which)
    {
        playerNetworkData &d = controllerNetworkData[0];
        switch (which)
        {
        case RAW_NORMAL:
            return (unsigned char *)d.playerStats;
        case RAW_BASICTRAINING:
            return (unsigned char *)d.basicTrainingStats;
        default:
            return (unsigned char *)d.globalplayerStats;
        }
    }

    // flag bytes that directly follow each stats buffer (see LiveStorage_Set* in live_storage.cpp)
    void RawSetFlags(unsigned char *buf, bool checksumValid, bool writeNeeded, bool ddlValidated, bool fetched)
    {
        buf[kStatsSize + 0] = checksumValid ? 1 : 0;
        buf[kStatsSize + 1] = writeNeeded ? 1 : 0;
        buf[kStatsSize + 2] = ddlValidated ? 1 : 0;
        buf[kStatsSize + 3] = fetched ? 1 : 0;
    }

    bool BuildStatsPath(const char *fileName, char *path, size_t pathSize, bool makeDir)
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
            _mkdir(dir); // fails harmlessly when it already exists
        }

#ifdef KISAK_WEB
        _snprintf(path, pathSize, "%s/players/%s", home, fileName);
#else
        _snprintf(path, pathSize, "%s\\players\\%s", home, fileName);
#endif
        path[pathSize - 1] = 0;
        return true;
    }

    bool ReadStatsFile(const char *fileName, unsigned int expectedCount, unsigned char *dst)
    {
        char path[512];
        BuildStatsPath(fileName, path, sizeof(path), false);

        FILE *f = fopen(path, "rb");
        if (!f)
            return false;

        OfflineStatsFileHeader hdr;
        bool ok = fread(&hdr, sizeof(hdr), 1, f) == 1
            && hdr.magic == kFileMagic
            && hdr.version == kFileVersion
            && hdr.bufferSize == (unsigned int)kStatsSize
            && hdr.bufferCount == expectedCount;
        if (ok)
            ok = fread(dst, kStatsSize, expectedCount, f) == expectedCount;
        fclose(f);

        if (!ok)
            Com_PrintWarning(16, "LiveOffline: stats file '%s' is invalid, ignoring it\n", path);
        return ok;
    }

    // temp file + rename so a crash in the middle of a write never destroys the previous save
    bool WriteStatsFileAtomic(const char *fileName, const unsigned char *a, const unsigned char *b)
    {
        char path[512];
        char tmpPath[528];
        BuildStatsPath(fileName, path, sizeof(path), true);
        _snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
        tmpPath[sizeof(tmpPath) - 1] = 0;

        OfflineStatsFileHeader hdr;
        hdr.magic = kFileMagic;
        hdr.version = kFileVersion;
        hdr.bufferSize = kStatsSize;
        hdr.bufferCount = b ? 2 : 1;

        FILE *f = fopen(tmpPath, "wb");
        if (!f)
        {
            Com_PrintWarning(16, "LiveOffline: could not open '%s' for writing\n", tmpPath);
            return false;
        }

        bool ok = fwrite(&hdr, sizeof(hdr), 1, f) == 1 && fwrite(a, kStatsSize, 1, f) == 1;
        if (ok && b)
            ok = fwrite(b, kStatsSize, 1, f) == 1;
        if (fflush(f) != 0)
            ok = false;
        if (fclose(f) != 0)
            ok = false;

        if (ok)
            ok = MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

        if (!ok)
        {
            Com_PrintWarning(16, "LiveOffline: failed to write stats file '%s'\n", path);
            remove(tmpPath);
        }
        return ok;
    }

    // Same idea as SV_IsStatsBlobOK: associate / upgrade the buffer to the current stats DDL.
    bool StatsBlobMatchesDDL(unsigned char *buf)
    {
        if (DDL_AssociateBuffer((char *)buf, kStatsSize, g_statsDDL))
            return true;
        if (DDL_FixBufferVersion((char *)buf, g_statsDDL, "ddl_mp/stats.ddl", (char *)s_ddlBackup, kStatsSize)
            || DDL_FixBufferVersion((char *)buf, g_statsDDL, "ddl_mp/stats_archive.ddl", (char *)s_ddlBackup, kStatsSize))
        {
            DDL_NoCheckPrintWarning("DDL: Stats buffer updated to version %d\n", g_statsDDL->version);
            LiveStats_WriteChecksumToBuffer(buf, kStatsSize);
            return true;
        }
        return false;
    }

    bool ValidateLoadedBuffer(unsigned char *buf, bool wantBasicTraining, const char *what)
    {
        if (!LiveStats_ValidateChecksum(buf, kStatsSize - 4))
        {
            Com_PrintWarning(16, "LiveOffline: %s stats checksum is invalid\n", what);
            return false;
        }
        if (!StatsBlobMatchesDDL(buf))
        {
            Com_PrintWarning(16, "LiveOffline: %s stats do not match the stats DDL\n", what);
            return false;
        }
        const unsigned int version = LiveStats_ReadVersionFromBuffer((char *)buf);
        if (version != stat_version->current.unsignedInt)
        {
            Com_Printf(16, "LiveOffline: %s stats version is %u, expected %u - starting a new profile\n",
                what, version, stat_version->current.unsignedInt);
            return false;
        }
        if (LiveStats_GetBasicTrainingState(buf) != wantBasicTraining)
        {
            Com_PrintWarning(16, "LiveOffline: %s stats have the wrong basic training state\n", what);
            return false;
        }
        return true;
    }

    // fresh NORMAL + GLOBAL buffers: same as the original "no stats found" flow
    void InitFreshMpProfile()
    {
        Com_Printf(16, "LiveOffline: creating a new multiplayer stats profile\n");

        // GLOBAL first: stats_init.cfg writes permission-2 stats (xp, unlocks, ...) into it
        LiveStorage_ResetStats(RawBuf(RAW_GLOBAL));
        RawSetFlags(RawBuf(RAW_GLOBAL), true, false, true, true);

        // NORMAL: memset + DDL associate + exec mp/stats_init.cfg + version/xuid/online ranked flags
        RawSetFlags(RawBuf(RAW_NORMAL), true, false, true, true);
        LiveStats_ResetStats(0, false);
        RawSetFlags(RawBuf(RAW_NORMAL), true, false, true, true);
    }

    void InitFreshBasicTrainingProfile()
    {
        Com_Printf(16, "LiveOffline: creating a new basic training stats profile\n");

        RawSetFlags(RawBuf(RAW_BASICTRAINING), true, false, true, true);
        LiveStats_ResetBasicTrainingStats(0);
        RawSetFlags(RawBuf(RAW_BASICTRAINING), true, false, true, true);
    }

    const unsigned char *GetFreshBuffer()
    {
        if (!s_freshBufferValid)
        {
            LiveStorage_ResetStats(s_freshBuffer);
            s_freshBufferValid = true;
        }
        return s_freshBuffer;
    }

    struct RemoteProfile
    {
        unsigned __int64 xuid = 0;
        bool ready = false;
        unsigned int requestedAt = 0;
        size_t received = 0;
        std::vector<unsigned char> upload;
    };
    std::map<client_t *, RemoteProfile> s_remoteProfiles;
    unsigned char s_clientUpload[kStatsSize * 3];
    size_t s_clientUploadOffset = sizeof(s_clientUpload);
    bool s_clientUploadStarted = false;

    bool RemoteHuman(const client_t *client)
    {
        return client && client->steamID && !client->bIsTestClient && !client->bIsDemoClient
            && client->header.netchan.remoteAddress.type == NA_IP;
    }

    void RemoteFileName(client_t *client, const char *file, char *name, size_t size, bool makeDir)
    {
        _snprintf(name, size, "by_xuid/%016llx/%s", (unsigned long long)client->steamID, file);
        name[size - 1] = 0;
        if (makeDir)
        {
            char path[512];
            BuildStatsPath("by_xuid", path, sizeof(path), true);
            _mkdir(path);
            char dir[64];
            _snprintf(dir, sizeof(dir), "by_xuid/%016llx", (unsigned long long)client->steamID);
            BuildStatsPath(dir, path, sizeof(path), false);
            _mkdir(path);
        }
    }

    bool LoadRemoteProfile(client_t *client)
    {
        RemoteProfile &profile = s_remoteProfiles[client];
        if (profile.xuid != client->steamID) profile = RemoteProfile();
        profile.xuid = client->steamID;
        char name[128];
        RemoteFileName(client, kMpStatsFile, name, sizeof(name), false);
        if (!ReadStatsFile(name, 2, s_fileBuf[0])
            || !ValidateLoadedBuffer(s_fileBuf[0], false, "remote normal")
            || !ValidateLoadedBuffer(s_fileBuf[1], false, "remote global"))
        {
            profile.ready = false;
            profile.received = 0;
            profile.upload.clear();
            return false;
        }
        if (Dvar_GetBool("xblive_basictraining"))
        {
            RemoteFileName(client, kBtStatsFile, name, sizeof(name), false);
            if (ReadStatsFile(name, 1, s_fileBuf[2]) && ValidateLoadedBuffer(s_fileBuf[2], true, "remote basic training"))
                memcpy(client->stats, s_fileBuf[2], kStatsSize);
            else
            {
                memcpy(client->stats, GetFreshBuffer(), kStatsSize);
                LiveStats_SetBasicTrainingState(client->stats, true);
            }
        }
        else memcpy(client->stats, s_fileBuf[0], kStatsSize);
        memcpy(client->globalStats, s_fileBuf[1], kStatsSize);
        profile.ready = true;
        Com_Printf(16, "LiveOffline: restored host stats for %016llx\n", (unsigned long long)client->steamID);
        return true;
    }

    client_t *FindLocalClient()
    {
        if (!svs.clients || !com_maxclients)
            return NULL;
        if (!com_sv_running || !com_sv_running->current.enabled)
            return NULL;

        for (int i = 0; i < com_maxclients->current.integer; ++i)
        {
            client_t *cl = &svs.clients[i];
            if (cl->header.state == CS_FREE || cl->bIsTestClient || cl->bIsDemoClient)
                continue;
            if (cl->header.netchan.remoteAddress.type == NA_LOOPBACK)
                return cl;
        }
        return NULL;
    }
}

bool LiveOffline_StatsLoaded()
{
    return s_loaded;
}

bool LiveOffline_LoadStats(int controllerIndex)
{
    if (controllerIndex != 0)
        return false;
    if (s_loading)
        return s_loaded;
    if (s_loaded)
        return true;

    if (!g_statsDDL)
    {
        Com_PrintWarning(16, "LiveOffline: stats DDL is not loaded yet, cannot load local stats\n");
        return false;
    }
    if (!stat_version || !stat_version->current.integer || !xblive_basictraining)
    {
        Com_PrintWarning(16, "LiveOffline: stats dvars are not registered yet, cannot load local stats\n");
        return false;
    }
    if (!ui_items_no_cost || !custom_killstreak_mode || !ui_useCustomClassInfo)
    {
        Com_PrintWarning(16, "LiveOffline: ui dvars are not registered yet, cannot load local stats\n");
        return false;
    }
    if (!LiveOffline_IsSignedIn())
    {
        Com_PrintWarning(16, "LiveOffline: not signed in, cannot load local stats\n");
        return false;
    }

    s_loading = true;

    // everything below addresses the NORMAL (online ranked) buffers explicitly
    const bool wasBasicTraining = xblive_basictraining->current.enabled;
    Dvar_SetBool((dvar_s *)xblive_basictraining, false);

    bool mpOk = false;
    bool btOk = false;

    if (ReadStatsFile(kMpStatsFile, 2, s_fileBuf[0]))
    {
        mpOk = ValidateLoadedBuffer(s_fileBuf[0], false, "multiplayer")
            && ValidateLoadedBuffer(s_fileBuf[1], false, "global");
    }
    if (ReadStatsFile(kBtStatsFile, 1, s_fileBuf[2]))
    {
        btOk = ValidateLoadedBuffer(s_fileBuf[2], true, "basic training");
    }

    if (mpOk)
    {
        memcpy(RawBuf(RAW_NORMAL), s_fileBuf[0], kStatsSize);
        memcpy(RawBuf(RAW_GLOBAL), s_fileBuf[1], kStatsSize);
        RawSetFlags(RawBuf(RAW_NORMAL), true, false, true, true);
        RawSetFlags(RawBuf(RAW_GLOBAL), true, false, true, true);
        Com_Printf(16, "LiveOffline: loaded local multiplayer stats\n");
    }
    if (btOk)
    {
        memcpy(RawBuf(RAW_BASICTRAINING), s_fileBuf[2], kStatsSize);
        RawSetFlags(RawBuf(RAW_BASICTRAINING), true, false, true, true);
        Com_Printf(16, "LiveOffline: loaded local basic training stats\n");
    }

    LiveStats_SetBufferInitialised(0, true);

    if (!mpOk)
        InitFreshMpProfile();
    if (!btOk)
        InitFreshBasicTrainingProfile();

    LiveStats_SetBufferInitialised(0, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_NORMAL, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_FORCE_NORMAL, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_BASICTRAINING, true);
    LiveStats_MakeStableStatsBuffer(0);
    LiveStats_MakeStableGlobalStatsBuffer(0);

    Dvar_SetBool((dvar_s *)xblive_basictraining, wasBasicTraining);

    // we have a profile now (loaded or freshly created): this is not a "first run" any more
    LiveStorage_SetFirstTimeRunning(false);
    controllerNetworkData[0].firstTimeRunning = false;

    s_loading = false;
    s_loaded = true;

    if (!mpOk || !btOk)
        LiveOffline_SaveStats(0); // persist the freshly created profile(s) right away

    return true;
}

void LiveOffline_ResetProfile()
{
    if (!s_loaded || s_loading)
        return;

    s_loading = true;

    const bool wasBasicTraining = xblive_basictraining->current.enabled;
    Dvar_SetBool((dvar_s *)xblive_basictraining, false);

    InitFreshMpProfile();
    InitFreshBasicTrainingProfile();

    LiveStats_SetBufferInitialised(0, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_NORMAL, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_FORCE_NORMAL, true);
    LiveStorage_SetStatsFetched(0, STATS_LOCATION_BASICTRAINING, true);
    LiveStats_MakeStableStatsBuffer(0);
    LiveStats_MakeStableGlobalStatsBuffer(0);

    Dvar_SetBool((dvar_s *)xblive_basictraining, wasBasicTraining);

    s_serverFilledLocal = false; // a running local server must not write its (old) copy back
    s_loading = false;

    LiveOffline_SaveStats(0);
}

void LiveOffline_SyncStatsFromLocalServer()
{
    if (!s_loaded || s_loading || !s_serverFilledLocal)
        return;

    client_t *cl = FindLocalClient();
    if (!cl)
        return;
    if (cl->statPacketsReceived != ALL_STATS_PACKETS_RECEIVED)
        return;

    // Basic training games keep everything in cl->stats, normal games modify cl->globalStats
    // (see SV_SetClientD*Stat); cl->stats of a normal game is only the (read-only) CAC data.
    const unsigned char *serverBuf = s_serverFilledWasBT ? cl->stats : cl->globalStats;
    if (memcmp(serverBuf, s_serverFillSnapshot, kStatsSize) == 0)
        return; // server did not change anything since it received the stats

    unsigned char *dst = RawBuf(s_serverFilledWasBT ? RAW_BASICTRAINING : RAW_GLOBAL);
    memcpy(dst, serverBuf, kStatsSize);
    memcpy(s_serverFillSnapshot, serverBuf, kStatsSize);
    LiveStats_WriteChecksumToBuffer(dst, kStatsSize);
    Com_Printf(16, "LiveOffline: stats of the local player were updated by the server\n");
}

void LiveOffline_SaveStats(int controllerIndex)
{
    if (controllerIndex != 0 || !s_loaded || s_loading)
        return;

    LiveOffline_SyncStatsFromLocalServer();

    unsigned char *norm = RawBuf(RAW_NORMAL);
    unsigned char *glob = RawBuf(RAW_GLOBAL);
    unsigned char *bt = RawBuf(RAW_BASICTRAINING);

    LiveStats_WriteChecksumToBuffer(norm, kStatsSize);
    LiveStats_WriteChecksumToBuffer(glob, kStatsSize);
    LiveStats_WriteChecksumToBuffer(bt, kStatsSize);

    if (WriteStatsFileAtomic(kMpStatsFile, norm, glob))
    {
        norm[kStatsSize + 1] = 0; // writeNeeded
        glob[kStatsSize + 1] = 0;
    }
    if (WriteStatsFileAtomic(kBtStatsFile, bt, NULL))
        bt[kStatsSize + 1] = 0;
}

void LiveOffline_FillServerClientStats(client_t *client)
{
    if (RemoteHuman(client) && client->header.state >= CS_CLIENTLOADING)
        LiveOffline_SaveClientStats(client);
    const bool basicTraining = xblive_basictraining && xblive_basictraining->current.enabled;
    const bool isLocal = s_loaded
        && !client->bIsTestClient
        && !client->bIsDemoClient
        && client->header.netchan.remoteAddress.type == NA_LOOPBACK;

    memset(client->modifiedStatBytes, 0, sizeof(client->modifiedStatBytes));
    client->statsModified = 0;
    client->statsSentIndex = 0;

    if (isLocal)
    {
        // gamestate is sent again on map_restart / map changes: keep what the server changed so far
        LiveOffline_SyncStatsFromLocalServer();

        // the local player's stats come straight from the local buffers (no network transfer)
        memcpy(client->stats, RawBuf(basicTraining ? RAW_BASICTRAINING : RAW_NORMAL), kStatsSize);
        if (basicTraining)
            memcpy(client->globalStats, GetFreshBuffer(), kStatsSize);
        else
            memcpy(client->globalStats, RawBuf(RAW_GLOBAL), kStatsSize);

        s_serverFilledLocal = true;
        s_serverFilledWasBT = basicTraining;
        memcpy(s_serverFillSnapshot, basicTraining ? client->stats : client->globalStats, kStatsSize);
    }
    else
    {
        // Bots remain empty; a returning human uses the host record.
        memcpy(client->stats, GetFreshBuffer(), kStatsSize);
        memcpy(client->globalStats, GetFreshBuffer(), kStatsSize);
        if (RemoteHuman(client))
        {
            LoadRemoteProfile(client);
        }
    }

    memcpy(client->globalStatsStable, client->globalStats, kStatsSize);
    client->statsValidated = 1;

    memset(client->purchasedItems, 0, sizeof(client->purchasedItems));
    BG_UnlockablesSetPurchasedBits(client->stats, client->purchasedItems);

    client->statPacketsReceived = ALL_STATS_PACKETS_RECEIVED;
}

void LiveOffline_SaveClientStats(client_t *client)
{
    if (!RemoteHuman(client) || client->statPacketsReceived != ALL_STATS_PACKETS_RECEIVED)
        return;
    auto it = s_remoteProfiles.find(client);
    if (it == s_remoteProfiles.end() || !it->second.ready || it->second.xuid != client->steamID)
        return; // Never persist the temporary empty profile of an interrupted first upload.
    char file[128];
    LiveStats_WriteChecksumToBuffer(client->stats, kStatsSize);
    LiveStats_WriteChecksumToBuffer(client->globalStats, kStatsSize);
    if (Dvar_GetBool("xblive_basictraining"))
    {
        RemoteFileName(client, kBtStatsFile, file, sizeof(file), true);
        WriteStatsFileAtomic(file, client->stats, nullptr);
    }
    else
    {
        RemoteFileName(client, kMpStatsFile, file, sizeof(file), true);
        WriteStatsFileAtomic(file, client->stats, client->globalStats);
    }
    RemoteFileName(client, "name.txt", file, sizeof(file), false);
    char path[512];
    BuildStatsPath(file, path, sizeof(path), false);
    if (FILE *f = fopen(path, "wb"))
    {
        fwrite(client->name, 1, strnlen(client->name, sizeof(client->name)), f);
        fclose(f);
    }
}

bool LiveOffline_ClientStatsReady(client_t *client)
{
    if (!RemoteHuman(client)) return true;
    auto it = s_remoteProfiles.find(client);
    return it != s_remoteProfiles.end() && it->second.xuid == client->steamID && it->second.ready;
}

void LiveOffline_ForgetClientStats(client_t *client)
{
    s_remoteProfiles.erase(client);
}

void LiveOffline_SaveServerStats()
{
    if (!svs.clients || !com_maxclients) return;
    for (int i = 0; i < com_maxclients->current.integer; ++i)
        if (svs.clients[i].header.state >= CS_CONNECTED)
            LiveOffline_SaveClientStats(&svs.clients[i]);
    LiveOffline_SaveStats(0);
}

void LiveOffline_BeginClientStatsUpload(int localClientNum)
{
    if (s_clientUploadStarted) return;
    clientConnection_t *connection = CL_GetLocalClientConnection(localClientNum);
    if (!s_loaded || connection->serverAddress.type != NA_IP) return;
    s_clientUploadStarted = true;
    memcpy(s_clientUpload, RawBuf(RAW_NORMAL), kStatsSize);
    memcpy(s_clientUpload + kStatsSize, RawBuf(RAW_GLOBAL), kStatsSize);
    memcpy(s_clientUpload + kStatsSize * 2, RawBuf(RAW_BASICTRAINING), kStatsSize);
    LiveStats_WriteChecksumToBuffer(s_clientUpload + kStatsSize * 2, kStatsSize);
    LiveStats_WriteChecksumToBuffer(s_clientUpload, kStatsSize);
    LiveStats_WriteChecksumToBuffer(s_clientUpload + kStatsSize, kStatsSize);
    s_clientUploadOffset = 0;
}

void LiveOffline_ResetClientStatsUpload()
{
    s_clientUploadStarted = false;
    s_clientUploadOffset = sizeof(s_clientUpload);
}

void LiveOffline_RequestClientStats(client_t *client)
{
    auto it = s_remoteProfiles.find(client);
    if (it == s_remoteProfiles.end() || it->second.ready) return;
    const unsigned int now = Sys_Milliseconds();
    if (it->second.requestedAt && now - it->second.requestedAt < 1000) return;
    it->second.requestedAt = now;
    NET_OutOfBandPrint(NS_SERVER, client->header.netchan.remoteAddress, "kisak_stats_request");
}

void LiveOffline_PumpClientStatsUpload(int localClientNum)
{
    clientConnection_t *connection = CL_GetLocalClientConnection(localClientNum);
    // Keep the 2 KB engine packet and reliable ring bounded, even on a slow or lossy peer.
    if (s_clientUploadOffset >= sizeof(s_clientUpload) || connection->serverAddress.type != NA_IP
        || connection->reliableSequence != connection->reliableAcknowledge) return;
    const size_t count = std::min(size_t(384), sizeof(s_clientUpload) - s_clientUploadOffset);
    char hex[769];
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i)
    {
        const unsigned char byte = s_clientUpload[s_clientUploadOffset + i];
        hex[i * 2] = digits[byte >> 4]; hex[i * 2 + 1] = digits[byte & 15];
    }
    hex[count * 2] = 0;
    CL_AddReliableCommand(localClientNum, va("kisak_stats %u %s", (unsigned)s_clientUploadOffset, hex));
    s_clientUploadOffset += count;
}

bool LiveOffline_HandleStatsCommand(client_t *client)
{
    if (strcmp(SV_Cmd_Argv(0), "kisak_stats")) return false;
    if (!RemoteHuman(client)) return true;
    auto it = s_remoteProfiles.find(client);
    if (it == s_remoteProfiles.end() || it->second.xuid != client->steamID || it->second.ready) return true;
    RemoteProfile &profile = it->second;
    char *end = nullptr;
    const unsigned long offset = strtoul(SV_Cmd_Argv(1), &end, 10);
    const char *hex = SV_Cmd_Argv(2);
    const size_t len = strlen(hex);
    if (!end || *end || offset != profile.received || !len || len > 768 || (len & 1)
        || offset + len / 2 > size_t(kStatsSize * 3)) return true;
    if (profile.upload.empty()) profile.upload.resize(kStatsSize * 3);
    for (size_t i = 0; i < len; ++i)
    {
        const char *digit = strchr("0123456789abcdef", hex[i]);
        if (!digit) return true;
    }
    for (size_t i = 0; i < len / 2; ++i)
    {
        const int hi = (int)(strchr("0123456789abcdef", hex[i * 2]) - "0123456789abcdef");
        const int lo = (int)(strchr("0123456789abcdef", hex[i * 2 + 1]) - "0123456789abcdef");
        profile.upload[offset + i] = (unsigned char)((hi << 4) | lo);
    }
    profile.received += len / 2;
    if (profile.received != profile.upload.size()) return true;
    unsigned char *normal = profile.upload.data(), *global = normal + kStatsSize, *bt = global + kStatsSize;
    if (!ValidateLoadedBuffer(normal, false, "uploaded normal") || !ValidateLoadedBuffer(global, false, "uploaded global")
        || !ValidateLoadedBuffer(bt, true, "uploaded basic training"))
    {
        profile.upload.clear();
        return true;
    }
    char file[128];
    RemoteFileName(client, kMpStatsFile, file, sizeof(file), true);
    WriteStatsFileAtomic(file, normal, global);
    RemoteFileName(client, kBtStatsFile, file, sizeof(file), true);
    WriteStatsFileAtomic(file, bt, nullptr);
    memcpy(client->stats, Dvar_GetBool("xblive_basictraining") ? bt : normal, kStatsSize);
    memcpy(client->globalStats, global, kStatsSize);
    memcpy(client->globalStatsStable, global, kStatsSize);
    BG_UnlockablesSetPurchasedBits(client->stats, client->purchasedItems);
    profile.ready = true;
    profile.upload.clear();
    LiveOffline_SaveClientStats(client);
    Com_Printf(16, "LiveOffline: imported first-visit stats for %016llx\n", (unsigned long long)client->steamID);
    return true;
}

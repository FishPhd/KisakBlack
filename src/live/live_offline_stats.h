#pragma once

// Offline (no DemonWare) local stats store / progression.
// Implements LiveOffline_LoadStats / LiveOffline_SaveStats (declared in live_offline.h) plus the
// helpers the local listen-server uses to receive the local player's stats without any network
// transfer (replaces the DemonWare "CAC / global stats" download done in the original game).

struct client_t;

// True once LiveOffline_LoadStats has succeeded (buffers are loaded / freshly initialised).
bool LiveOffline_StatsLoaded();

// Pump native DDL dirty flags in menus as well as gameplay.
void LiveOffline_StatsFrame();

// Mirror a successful native purchase/refund into the offline reward buffers.
void LiveOffline_OnCurrencySpent(int controllerIndex, int amount);

// Server side: fills client->stats / globalStats / globalStatsStable / purchasedItems and marks
// client->statPacketsReceived = ALL_STATS_PACKETS_RECEIVED.
//  - local (loopback) human client: copy of the local player's NORMAL (or BASICTRAINING) and GLOBAL buffers
//  - remote humans: host record, or an empty buffer until their first validated upload
//  - bots: fresh empty (version-stamped) stats buffers
// Called from SV_SendClientGameState (replaces the DemonWare stats download).
void LiveOffline_FillServerClientStats(client_t *client);

// Wipes the local profile (both the normal and the basic training stats) and saves the fresh files.
// Used by the "resetstats" command (LiveStorage_DeleteGlobalStats).
void LiveOffline_ResetProfile();

// Copies the (possibly modified) server-side stats of the local loopback client back into the
// local GLOBAL / BASICTRAINING buffers. Called automatically by LiveOffline_SaveStats.
void LiveOffline_SyncStatsFromLocalServer();

// Remote humans keep progression on the host; test/demo/loopback clients are excluded.
void LiveOffline_SaveClientStats(client_t *client);
void LiveOffline_SaveServerStats();
void LiveOffline_BeginClientStatsUpload(int localClientNum);
void LiveOffline_ResetClientStatsUpload();
void LiveOffline_RequestClientStats(client_t *client);
void LiveOffline_PumpClientStatsUpload(int localClientNum);
bool LiveOffline_HandleStatsCommand(client_t *client);
bool LiveOffline_ClientStatsReady(client_t *client);
void LiveOffline_ForgetClientStats(client_t *client);

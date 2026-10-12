#pragma once

// Offline (no Steam / no DemonWare) local identity + sign-in.
// Used when KISAK_LIVE is not defined.

// Signs the local user in: loads/creates the local profile identity, fills xenonUserData[0],
// sets xblive_loggedin / dw_loggedin, loads local stats (LiveOffline_LoadStats) and marks
// playlists / contracts / time sync as ready. Safe to call repeatedly (idempotent).
// Returns true when the user is signed in.
bool LiveOffline_SignIn(int controllerIndex);

// True once LiveOffline_SignIn succeeded.
bool LiveOffline_IsSignedIn();

// Local identity.
unsigned long long LiveOffline_GetXuid();
const char *LiveOffline_GetGamertag();

// True once stats buffers are loaded and fetched flags are set.
bool LiveOffline_AreStatsReady();

// Implemented in live_offline_stats.cpp (separate agent):
// Loads stats files into the NORMAL and BASICTRAINING buffers, or initialises a fresh profile via
// exec mp/stats_init.cfg. Returns true when buffers are ready and fetched flags are set.
bool LiveOffline_LoadStats(int controllerIndex);
// Writes the NORMAL and BASICTRAINING stats buffers to disk.
void LiveOffline_SaveStats(int controllerIndex);

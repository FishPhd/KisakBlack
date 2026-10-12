#include "live_steam_achievements.h"

// Offline build: no achievements, no console commands (livesteam_giveachivement /
// livesteam_resetachivement are intentionally NOT registered), no callbacks.

void __cdecl LiveSteam_Achievements_Init()
{
}

void __cdecl LiveSteam_GiveAchievement_f()
{
}

void __cdecl LiveSteam_ResetAchievement_f()
{
}

void __cdecl LiveSteam_Achievements_Shutdown()
{
}

void __cdecl LiveSteam_Achievements_Frame()
{
}

void __cdecl LiveSteam_GiveAchievement(const char *achievementName)
{
}

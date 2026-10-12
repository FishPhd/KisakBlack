#pragma once

void __cdecl LiveSteam_Achievements_Init();
void __cdecl LiveSteam_GiveAchievement_f();
void __cdecl LiveSteam_ResetAchievement_f();
void __cdecl LiveSteam_Achievements_Shutdown();
void __cdecl LiveSteam_Achievements_Frame();
void __cdecl LiveSteam_GiveAchievement(const char *achievementName);

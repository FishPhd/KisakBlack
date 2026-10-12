@echo off
rem Host a LAN dedicated server. Run from the folder containing KisakBlack_Release.exe.
cd /d "%~dp0"
set MAP=%1
if "%MAP%"=="" set MAP=mp_nuked
start "" KisakBlack_Release.exe +set dedicated 1 +set net_ip 0.0.0.0 +set net_port 3074 +set sv_maxclients 12 +set g_gametype tdm +map %MAP%

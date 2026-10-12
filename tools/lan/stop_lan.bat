@echo off
rem Kills every running KisakBlack process (server and clients). Safe to run any time.
taskkill /F /IM KisakBlack_Release.exe /T
taskkill /F /IM KisakBlack.exe /T
exit /b 0

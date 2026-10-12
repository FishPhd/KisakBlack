@echo off
rem Creates ..\build_p2 : a second game folder for running two clients on one PC.
rem  - zone\ is a junction, main\*.iwd are hard links (no extra disk space, no admin needed)
rem  - players\ and main\*.log are separate, so client B gets its own profile/XUID and log
rem Run from the build folder (needs to be on the same drive as build_p2).
setlocal
cd /d "%~dp0"
set SRC=%CD%
set DST=%CD%\..\build_p2
if not exist "%SRC%\KisakBlack_Release.exe" (echo KisakBlack_Release.exe not found in %SRC% & exit /b 1)

mkdir "%DST%" 2>nul
mkdir "%DST%\main" 2>nul
mkdir "%DST%\players" 2>nul

copy /Y "%SRC%\KisakBlack_Release.exe" "%DST%\" >nul
copy /Y "%SRC%\*.dll" "%DST%\" >nul 2>nul
copy /Y "%SRC%\stop_lan.bat" "%DST%\" >nul
rem localization.txt defines the language list (without it the zone path becomes zone\(null)\...)
copy /Y "%SRC%\localization.txt" "%DST%\" >nul
copy /Y "%SRC%\*.bmp" "%DST%\" >nul 2>nul

if not exist "%DST%\zone" mklink /J "%DST%\zone" "%SRC%\zone"

for %%F in ("%SRC%\main\*.iwd") do (
  if not exist "%DST%\main\%%~nxF" mklink /H "%DST%\main\%%~nxF" "%%F" >nul
)
if exist "%SRC%\main\video" if not exist "%DST%\main\video" mklink /J "%DST%\main\video" "%SRC%\main\video"

rem NOTE: players\kisak_profile.dat is deliberately NOT copied -> a new XUID is generated on first run.
echo.
echo Done: %DST%
echo Start client B with  join_p2.bat  (in the build folder) after the server and client A are up.
exit /b 0

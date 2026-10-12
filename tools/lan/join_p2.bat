@echo off
rem Starts client B (second window) from ..\build_p2 and joins a server. Usage: join_p2.bat [host-ip] [minutes]
rem Auto-kills after [minutes] (default 10). Manual kill: stop_lan.bat from another cmd window.
setlocal
set HOSTIP=%1
if "%HOSTIP%"=="" set HOSTIP=127.0.0.1
set MINUTES=%2
if "%MINUTES%"=="" set MINUTES=10
if not exist "%~dp0..\build_p2\KisakBlack_Release.exe" (echo Run make_second_client.bat first. & exit /b 1)
cd /d "%~dp0..\build_p2"
start "" KisakBlack_Release.exe +set net_ip 0.0.0.0 +set r_fullscreen 0 +connect %HOSTIP%:3074
set /a SECS=%MINUTES%*60
echo Client B started. Auto-kill in %MINUTES% minute(s). Close this window to cancel the timer.
timeout /t %SECS% /nobreak >nul
call "%~dp0stop_lan.bat"

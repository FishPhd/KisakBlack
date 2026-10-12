@echo off
rem Join a LAN server: join_lan.bat <host-ip> [minutes]
rem The client is force-killed after [minutes] (default 10) so a black screen can never trap you.
rem To kill it sooner: Win+R, type  cmd , then run stop_lan.bat  (or Ctrl+Shift+Esc, End task).
cd /d "%~dp0"
set HOSTIP=%1
if "%HOSTIP%"=="" set /p HOSTIP=Host IP (e.g. 192.168.1.232): 
set MINUTES=%2
if "%MINUTES%"=="" set MINUTES=10
start "" KisakBlack_Release.exe +set net_ip 0.0.0.0 +connect %HOSTIP%:3074
set /a SECS=%MINUTES%*60
echo Client started. Auto-kill in %MINUTES% minute(s). Close this window to cancel the timer.
timeout /t %SECS% /nobreak >nul
call "%~dp0stop_lan.bat"

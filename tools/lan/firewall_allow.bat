@echo off
rem Run once as Administrator on the host PC.
net session >nul 2>&1
if errorlevel 1 (
    echo Please run this as Administrator.
    pause
    exit /b 1
)
netsh advfirewall firewall delete rule name="KisakBlack" >nul 2>&1
netsh advfirewall firewall add rule name="KisakBlack" dir=in action=allow protocol=UDP localport=3074-3084
pause

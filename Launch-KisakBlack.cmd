@echo off
setlocal
set "KISAK_RELEASE_DIR=%~dp0build-release"
if not exist "%KISAK_RELEASE_DIR%\KisakBlack.exe" (
    echo Desktop Release has not been built: "%KISAK_RELEASE_DIR%\KisakBlack.exe"
    pause
    exit /b 1
)
if not exist "%KISAK_RELEASE_DIR%\localization.txt" goto missing_data
if not exist "%KISAK_RELEASE_DIR%\main\" goto missing_data
if not exist "%KISAK_RELEASE_DIR%\zone\" goto missing_data
start "" /D "%KISAK_RELEASE_DIR%" "%KISAK_RELEASE_DIR%\KisakBlack.exe"
exit /b 0

:missing_data
echo Game data is missing from "%KISAK_RELEASE_DIR%". See README.md build instructions.
pause
exit /b 1

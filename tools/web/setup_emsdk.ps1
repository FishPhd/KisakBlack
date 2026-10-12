# Installs the Emscripten SDK version pinned in tools/web/emsdk-version.txt and
# prints the environment the `web` CMake presets need.
#
#   tools\web\setup_emsdk.ps1                 # installs into D:\emsdk (or $env:EMSDK)
#   tools\web\setup_emsdk.ps1 -Dir C:\emsdk
#
# Afterwards, in every shell that builds the web target:
#   $env:EMSDK = "<dir>"; . "$env:EMSDK\emsdk_env.ps1"
#   cmake --preset web; cmake --build --preset web
param(
    [string]$Dir = $(if ($env:EMSDK) { $env:EMSDK } else { "D:\emsdk" })
)
$ErrorActionPreference = "Stop"
$version = (Get-Content (Join-Path $PSScriptRoot "emsdk-version.txt") | Select-Object -First 1).Trim()

if (-not (Test-Path (Join-Path $Dir "emsdk.py"))) {
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git $Dir
}
Push-Location $Dir
try {
    git pull --ff-only 2>$null | Out-Null  # newer emsdk lists newer releases; harmless if offline
    python .\emsdk.py install $version
    python .\emsdk.py activate $version
} finally {
    Pop-Location
}
"emsdk $version ready in $Dir"
"  `$env:EMSDK = '$Dir'; . '$Dir\emsdk_env.ps1'"

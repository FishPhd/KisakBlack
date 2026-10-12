# One command for the browser client.
#
#   tools\web\play.ps1
#
# Uses EMSDK if it is already set, otherwise D:\emsdk. The game files default to
# D:\GitHub\KisakBlack\build (the desktop install). Prints the URL to open.
param(
    [string]$Assets = "D:\GitHub\KisakBlack\build",
    [int]$Port = 8080,
    [switch]$Build,
    [switch]$AgentTest
)
$ErrorActionPreference = "Stop"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
if (-not $env:EMSDK) { $env:EMSDK = "D:\emsdk" }
if (Test-Path "$env:EMSDK\emsdk_env.ps1") { . "$env:EMSDK\emsdk_env.ps1" }
if ($Build) {
    cmake --preset web
    cmake --build --preset web
}
if (-not (Test-Path $Assets)) { throw "game directory not found: $Assets" }
# Sibling .br files for boot and map fastfiles. Missing brotli is a warning inside the script
# (pip install brotli); the server still starts and sends a .br when one is already on disk.
try {
    & (Join-Path $PSScriptRoot "compress_gen.ps1") -Gen "build-web/assets-gen" -Assets $Assets
} catch {
    Write-Host "compress_gen: skipped ($($_.Exception.Message))"
    Write-Host "Install brotli with: pip install brotli"
}
Write-Host ""
Write-Host "Open http://localhost:$Port/?assets=/assets/&gen=/gen/"
Write-Host "Hardware acceleration: Chrome Settings, System, Use hardware acceleration when available."
Write-Host "Confirm at chrome://gpu that WebGL2 is Hardware accelerated (not SwiftShader)."
Write-Host "Two tabs: the host page shows a room id. The other tab adds &join=ROOM"
Write-Host ""
$serverArgs = @("tools/web/serve.mjs", "--root", "build-web", "--assets", $Assets, "--gen", "build-web/assets-gen", "--port", $Port)
if ($AgentTest) {
    $serverArgs += "--agent-test"
    Write-Host "Agent URL: http://127.0.0.1:$Port/?assets=/assets/&gen=/gen/&agent=1"
}
node @serverArgs

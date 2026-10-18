param(
    [string]$Python = 'python',
    [switch]$AnalysisTools
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = Join-Path $repo 'build-re'
$venv = Join-Path $work 'venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $venv)) {
    & $Python -m venv (Join-Path $work 'venv')
    if ($LASTEXITCODE) { throw 'Python 3.11 or newer is required. Pass -Python with its full path.' }
}
& $venv -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot 'requirements.txt')
if ($LASTEXITCODE) { throw 'Python dependency installation failed' }
if ($AnalysisTools) {
    # Official immutable release URLs. Cached archives are SHA-256 recorded for
    # provenance. No tools or global PATH changes outside this repository.
    $downloads = Join-Path $work 'downloads'
    $vendor = Join-Path $work 'vendor'
    New-Item -ItemType Directory -Force $downloads, $vendor | Out-Null
    $archives = @(
        @('ghidra_12.0.4_PUBLIC_20260303.zip', 'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.0.4_build/ghidra_12.0.4_PUBLIC_20260303.zip', 'ghidra_12.0.4_PUBLIC', 'c3b458661d69e26e203d739c0c82d143cc8a4a29d9e571f099c2cf4bda62a120'),
        @('OpenJDK21U-jdk_x64_windows_hotspot_21.0.10_7.zip', 'https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.10%2B7/OpenJDK21U-jdk_x64_windows_hotspot_21.0.10_7.zip', 'jdk-21.0.10+7', '08cae782814f027f8b159d6b68823f0f87422eb475c1a0ea1abc7a4357aaf11f')
    )
    foreach ($archive in $archives) {
        $target = Join-Path $downloads $archive[0]
        if (!(Test-Path -LiteralPath $target)) { Invoke-WebRequest -Uri $archive[1] -OutFile $target }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archive[3]) {
            throw "Archive checksum mismatch: $target"
        }
        if (!(Test-Path -LiteralPath (Join-Path $vendor $archive[2]))) {
            Expand-Archive -LiteralPath $target -DestinationPath $vendor
        }
        Get-FileHash -LiteralPath $target -Algorithm SHA256
    }
}
& $venv (Join-Path $PSScriptRoot 'kisak_re.py') doctor
exit $LASTEXITCODE

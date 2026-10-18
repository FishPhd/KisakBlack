param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = Join-Path $repo 'build-re'
$downloads = Join-Path $work 'downloads'
$vendor = Join-Path $work 'vendor'
$ghidra = Join-Path $vendor 'ghidra_12.0.4_PUBLIC'
$java = Join-Path $vendor 'jdk-21.0.10+7'
if (!(Test-Path "$ghidra/support/launch.bat") -or !(Test-Path "$java/bin/java.exe")) {
    throw 'Run tools/re/setup.ps1 -AnalysisTools first.'
}
New-Item -ItemType Directory -Force $downloads, $vendor | Out-Null
$commit = '1fc1d734db5924c9bc9067ae244cb9b94c94efcb'
$packages = @(
    @('bindiff8.msi', 'https://github.com/google/bindiff/releases/download/v8/bindiff8.msi', '688831e490bef8a20c9917048b693bad591f346b3b96489fc79ad8d20d7cb15f'),
    @('binexport-1fc1d734.zip', "https://github.com/google/binexport/archive/$commit.zip", '5358ad955b065b1484450f0d1c6d6cea8afbbc45c9bb0c647076b080df33207f'),
    @('gradle-8.14.3-bin.zip', 'https://services.gradle.org/distributions/gradle-8.14.3-bin.zip', 'bd71102213493060956ec229d946beee57158dbd89d0e62b91bca0fa2c5f3531')
)
foreach ($package in $packages) {
    $file = Join-Path $downloads $package[0]
    if (!(Test-Path -LiteralPath $file)) { Invoke-WebRequest $package[1] -OutFile $file }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package[2]) {
        throw "Checksum mismatch: $file"
    }
}
$engine = Join-Path $vendor 'bindiff/ProgramFiles/BinDiff/bin/bindiff.exe'
if (!(Test-Path -LiteralPath $engine)) {
    $msi = Join-Path $downloads 'bindiff8.msi'
    $destination = Join-Path $vendor 'bindiff'
    $log = Join-Path $downloads 'bindiff-extract.log'
    # Administrative extraction into build-re only; no system-wide installation.
    $process = Start-Process msiexec.exe -WindowStyle Hidden -Wait -PassThru -ArgumentList "/a `"$msi`" /qn TARGETDIR=`"$destination`" /l*v `"$log`""
    if ($process.ExitCode -ne 0) { throw "MSI extraction failed ($($process.ExitCode)); see $log. Run setup from an elevated PowerShell if Windows Installer requires it." }
}
$source = Join-Path $vendor "binexport-$commit"
$gradle = Join-Path $vendor 'gradle-8.14.3/bin/gradle.bat'
if (!(Test-Path -LiteralPath $source)) { Expand-Archive (Join-Path $downloads 'binexport-1fc1d734.zip') $vendor }
if (!(Test-Path -LiteralPath $gradle)) { Expand-Archive (Join-Path $downloads 'gradle-8.14.3-bin.zip') $vendor }
$oldJava = $env:JAVA_HOME
$oldGradle = $env:GRADLE_USER_HOME
$oldGhidra = $env:GHIDRA_INSTALL_DIR
try {
    $env:JAVA_HOME = $java
    $env:GRADLE_USER_HOME = Join-Path $work 'gradle-cache'
    $env:GHIDRA_INSTALL_DIR = $ghidra
    & $gradle --no-daemon -p "$source/java" buildExtension
    if ($LASTEXITCODE) { throw 'BinExport build failed' }
    $archive = Get-ChildItem "$source/java/dist" -Filter '*BinExport.zip' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (!$archive) { throw 'BinExport extension archive missing' }
    Expand-Archive -LiteralPath $archive.FullName -DestinationPath "$ghidra/Ghidra/Extensions" -Force -ErrorAction Stop
    # Expand-Archive can emit nested nonterminating errors for loaded JARs.
    # Verify every extracted file rather than printing Ready after a partial install.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive.FullName)
    try {
        foreach ($entry in $zip.Entries) {
            if (!$entry.Name) { continue }
            $installed = Join-Path "$ghidra/Ghidra/Extensions" $entry.FullName
            $stream = $entry.Open()
            $hasher = [System.Security.Cryptography.SHA256]::Create()
            # BitConverter works on Windows PowerShell 5.1; Convert.ToHexString needs .NET 5+.
            try { $expected = ([BitConverter]::ToString($hasher.ComputeHash($stream))).Replace('-', '') }
            finally { $stream.Dispose(); $hasher.Dispose() }
            if (!(Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $installed).Hash -ne $expected) {
                throw "Extension installation incomplete: $installed. Close Ghidra and rerun setup."
            }
        }
    } finally { $zip.Dispose() }
    @{ binexport_commit = $commit; bindiff_sha256 = (Get-FileHash $engine).Hash; extension_sha256 = (Get-FileHash $archive.FullName).Hash } |
        ConvertTo-Json | Set-Content (Join-Path $vendor 'binary-diff-tools.json')
} finally {
    $env:JAVA_HOME = $oldJava
    $env:GRADLE_USER_HOME = $oldGradle
    $env:GHIDRA_INSTALL_DIR = $oldGhidra
}
Write-Output "Ready: tools/re/re.ps1 binary-diff --candidate build-release/KisakBlack.exe"

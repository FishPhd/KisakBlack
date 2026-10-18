param(
    [string]$SevenZip = 'C:/Program Files/7-Zip/7z.exe',
    [string]$Archive
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = Join-Path $repo 'build-re'
$url = 'https://web.archive.org/web/20090814000000id_/https://download.microsoft.com/download/2/E/9/2E911956-F90F-4BFB-8231-E292A7B6F287/GRMSDK_EN_DVD.iso'
if (!$Archive) { $Archive = Join-Path $work 'downloads/GRMSDK_EN_DVD.iso' }
New-Item -ItemType Directory -Force (Split-Path $Archive) | Out-Null
if (!(Test-Path -LiteralPath $Archive)) {
    $partial = "$Archive.partial"
    Invoke-WebRequest -Uri $url -OutFile $partial -TimeoutSec 900
    Move-Item -LiteralPath $partial -Destination $Archive
}
# Archived Microsoft SDK 7.0 x86 media. SHA-1 is the archived published identity;
# SHA-256 pins the complete media actually extracted by this workflow.
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne '65739FB0874CC17EA6962D8CE7915364C7161FA106ED1BF1C917924C18AC63CA') {
    throw 'SDK media SHA-256 mismatch; refusing extraction'
}
if (!(Test-Path -LiteralPath $SevenZip)) { throw 'Pass -SevenZip with an installed 7z.exe path' }
$package = Join-Path $work 'vendor/sdk7-package'
$payload = Join-Path $work 'vendor/sdk7-payload'
$destination = Join-Path $work 'vendor/vc9'
& $SevenZip x -y "-o$package" $Archive 'Setup/vc_stdx86/*' | Out-Null
if ($LASTEXITCODE) { throw 'SDK package extraction failed' }
& $SevenZip x -y "-o$payload" (Join-Path $package 'Setup/vc_stdx86/vc_stdx86.cab') | Out-Null
if ($LASTEXITCODE) { throw 'SDK compiler cabinet extraction failed' }
# Read MSI tables without installing the product or running custom actions.
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase((Join-Path $package 'Setup/vc_stdx86/vc_stdx86.msi'), 0)
function Read-MsiTable([string]$table, [string[]]$columns) {
    $quoted = ($columns | ForEach-Object { '`' + $_ + '`' }) -join ','
    $view = $database.OpenView('SELECT ' + $quoted + ' FROM `' + $table + '`')
    [void]$view.Execute()
    while ($record = $view.Fetch()) {
        $row = @{}
        for ($i = 0; $i -lt $columns.Count; $i++) { $row[$columns[$i]] = $record.StringData($i + 1) }
        [pscustomobject]$row
    }
    [void]$view.Close()
}
$directories = @{}
Read-MsiTable 'Directory' @('Directory','Directory_Parent','DefaultDir') | ForEach-Object { $directories[$_.Directory] = $_ }
$components = @{}
Read-MsiTable 'Component' @('Component','Directory_') | ForEach-Object { $components[$_.Component] = $_.Directory_ }
function Get-RelativeDirectory([string]$id, [int]$depth = 0) {
    if ($id -eq 'TARGETDIR') { return '' }
    if ($depth -gt 64 -or !$directories.ContainsKey($id)) { throw 'Invalid MSI directory graph' }
    $node = $directories[$id]
    $parent = Get-RelativeDirectory $node.Directory_Parent ($depth + 1)
    $leaf = (($node.DefaultDir -split ':')[0] -split '\|')[-1]
    if ($leaf -eq '.') { return $parent }
    return "$parent/$leaf".TrimStart('/')
}
$rootPath = [IO.Path]::GetFullPath($destination).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
$copied = @{}
Read-MsiTable 'File' @('File','Component_','FileName') | ForEach-Object {
    $directory = Get-RelativeDirectory $components[$_.Component_]
    $name = ($_.FileName -split '\|')[-1]
    # Keep compiler/headers/libs and its PDB support. App-local CRT assemblies
    # are copied below; nothing is registered in the operating system.
    $relative = $null
    if ($directory -match '(?:^|/)Microsoft Visual Studio 9\.0/VC(?:/(.*))?$') { $relative = 'VC/' + $Matches[1] + '/' + $name }
    elseif ($directory -match '/Common7/IDE$' -and $name -match '^mspdb.*\.(dll|exe)$') { $relative = 'VC/bin/' + $name }
    if ($relative) {
        $target = [IO.Path]::GetFullPath((Join-Path $destination $relative))
        if (!$target.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) { throw 'MSI path escapes local compiler directory' }
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $payload $_.File) -Destination $target -Force
        $copied[$relative] = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$crt = Join-Path $destination 'VC/redist/x86/Microsoft.VC90.CRT'
if (Test-Path -LiteralPath $crt) { Copy-Item -LiteralPath $crt -Destination (Join-Path $destination 'VC/bin/Microsoft.VC90.CRT') -Recurse -Force }
$compiler = Join-Path $destination 'VC/bin/cl.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Compiler not present in extracted SDK' }
$manifest = @{ source = $url; archive_sha256 = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant();
    compiler_version = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion; files = $copied;
    status = 'candidate_toolchain_not_proven_original'; system_install_performed = $false }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
$manifest | Select-Object status,compiler_version,system_install_performed | ConvertTo-Json

param(
    [switch]$Quick,
    [string]$Candidate = ""
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '../../../..')).Path
$python = Join-Path $repo 'build-re/venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw 'Run tools/re/setup.ps1 first.' }
$arguments = @((Join-Path $here 'iterate.py'))
if ($Quick) { $arguments += '--quick' }
if ($Candidate) { $arguments += @('--candidate', $Candidate) }
& $python @arguments
exit $LASTEXITCODE

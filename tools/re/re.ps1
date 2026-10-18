param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Command)
$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot '../../build-re/venv/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { throw 'Run tools/re/setup.ps1 first.' }
& $python (Join-Path $PSScriptRoot 'kisak_re.py') @Command
exit $LASTEXITCODE

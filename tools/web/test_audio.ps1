param([string]$Sdk = $(if ($env:EMSDK) { $env:EMSDK } else { 'D:\emsdk' }))
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')
. (Join-Path $Sdk 'emsdk_env.ps1')
$testRoot = Join-Path (Get-Location) 'build-web/audio-test'
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
& $env:EMSDK_PYTHON (Join-Path $Sdk 'upstream/emscripten/emcc.py') src/web/audio/wma/wmadeci.c -O1 -fwrapv -w `
    '-sEXPORTED_FUNCTIONS=_Kisak_DecodeWmav2Ex,_Kisak_DecodeWmav2,_malloc,_free' `
    '-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16' -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=node `
    -o "$testRoot/decoder.mjs"
if ($LASTEXITCODE) { throw 'Decoder test build failed' }
ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=440:duration=0.5:sample_rate=44100' `
    -ac 1 -c:a wmav2 -b:a 48k -f wav -y "$testRoot/tone.wav"
if ($LASTEXITCODE) { throw 'WMAv2 tone generation failed' }
ffmpeg -hide_banner -loglevel error -i "$testRoot/tone.wav" -c:a pcm_s16le -f s16le -y "$testRoot/reference.pcm"
if ($LASTEXITCODE) { throw 'Reference decode failed' }
node tools/web/test_wma.mjs "$testRoot/decoder.mjs" "$testRoot/tone.wav" "$testRoot/reference.pcm"
if ($LASTEXITCODE) { throw 'Decoder regression failed' }

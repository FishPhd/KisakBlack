# Brotli-compress boot and map fastfiles / IWDs so serve.mjs can send Content-Encoding: br
# from the sibling .br file. Requires the Python package brotli.
# If it is missing, install it with:
#   pip install brotli
# serve.mjs still sends a sibling .br when that file already exists.
param(
    [string]$Gen = "build-web/assets-gen",
    [string]$Assets = ""
)
$ErrorActionPreference = "Stop"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
$genPath = if ([System.IO.Path]::IsPathRooted($Gen)) { $Gen } else { Join-Path (Get-Location) $Gen }
$assetPath = ""
if ($Assets) {
    $assetPath = if ([System.IO.Path]::IsPathRooted($Assets)) { $Assets } else { Join-Path (Get-Location) $Assets }
}
# python.bat (pyenv) cannot take a multi-line -c script: cmd.exe splits on the newline.
# The script is written to a temp file and run as `python file.py`.
$code = @'
import json, pathlib, sys
try:
    import brotli
except ImportError:
    print("compress_gen: Python package 'brotli' is not installed.")
    print("Install with: pip install brotli")
    print("serve.mjs still sends a sibling .br when that file already exists.")
    sys.exit(0)

gen = pathlib.Path(sys.argv[1])
assets = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
seen = set()
n = 0

def compress(p):
    global n
    if not p.is_file() or p.suffix.lower() not in {".iwd", ".ff"}:
        return
    key = str(p.resolve()).lower() if p.exists() else str(p).lower()
    if key in seen:
        return
    seen.add(key)
    br = p.with_name(p.name + ".br")
    if br.exists() and br.stat().st_mtime >= p.stat().st_mtime:
        return
    br.write_bytes(brotli.compress(p.read_bytes(), quality=5))
    n += 1
    print(p.name, br.stat().st_size)

def consider_entry(e):
    rel = e.get("path") or ""
    if not rel.lower().endswith((".ff", ".iwd")):
        return
    if e.get("gen"):
        compress(gen / pathlib.Path(rel).name)
    elif assets is not None:
        compress(assets / pathlib.Path(rel))

def collect(block):
    if not block:
        return
    for e in block.get("boot") or []:
        consider_entry(e)
    for es in (block.get("maps") or {}).values():
        for e in es or []:
            consider_entry(e)

manifest = gen / "manifest.json"
if manifest.is_file():
    m = json.loads(manifest.read_text(encoding="utf-8"))
    collect(m)
    collect(m.get("client"))
if gen.is_dir():
    for p in gen.iterdir():
        if p.suffix.lower() in {".iwd", ".ff"}:
            compress(p)
print("compressed", n)
'@
$py = Join-Path ([System.IO.Path]::GetTempPath()) ("kisak-compress-gen-" + [System.Guid]::NewGuid().ToString("n") + ".py")
[System.IO.File]::WriteAllText($py, $code)
try {
    python $py $genPath $assetPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Remove-Item -Force $py -ErrorAction SilentlyContinue
}

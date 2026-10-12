#!/usr/bin/env bash
# Installs the Emscripten SDK version pinned in tools/web/emsdk-version.txt.
#
#   tools/web/setup_emsdk.sh [dir]     # default: $EMSDK or ~/emsdk
#
# Afterwards, in every shell that builds the web target:
#   export EMSDK=<dir>; source "$EMSDK/emsdk_env.sh"
#   cmake --preset web && cmake --build --preset web
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
version="$(head -n1 "$here/emsdk-version.txt" | tr -d '[:space:]')"
dir="${1:-${EMSDK:-$HOME/emsdk}}"

[ -f "$dir/emsdk.py" ] || git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$dir"
(cd "$dir" && git pull --ff-only >/dev/null 2>&1 || true)
(cd "$dir" && python3 ./emsdk.py install "$version" && python3 ./emsdk.py activate "$version")
echo "emsdk $version ready in $dir"
echo "  export EMSDK=$dir; source $dir/emsdk_env.sh"

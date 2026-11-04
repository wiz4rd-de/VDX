#!/usr/bin/env bash
# Builds the browser module (SPEC §15.2): emcc -O3 -msimd128 -sAUDIO_WORKLET
# -sWASM_WORKERS -sENVIRONMENT=web,worker,worklet over vdx_core + web/worklet.cpp,
# producing ui/public/vdx.wasm and its loaders (vdx.js, vdx.aw.js).
# Requires an activated Emscripten SDK (source <emsdk>/emsdk_env.sh).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
build="${VDX_WEB_BUILD_DIR:-$root/build/web}"
if ! command -v emcc >/dev/null 2>&1; then
  echo "web/build.sh: emcc not found — install and activate the Emscripten SDK (source <emsdk>/emsdk_env.sh)" >&2
  exit 1
fi
generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
flags="-O3 -msimd128 -pthread -sWASM_WORKERS=1"
emcmake cmake -S "$root" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release \
  -DVDX_BUILD_WEB=ON -DVDX_BUILD_TESTS=OFF -DVDX_BUILD_PLUGIN=OFF \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" >/dev/null
cmake --build "$build" --target vdx_web --parallel
ls -l "$root/ui/public/vdx.js" "$root/ui/public/vdx.wasm" "$root/ui/public/"vdx.aw.js 2>/dev/null || ls -l "$root/ui/public"

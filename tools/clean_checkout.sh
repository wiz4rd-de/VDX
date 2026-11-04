#!/usr/bin/env bash
# AC-B1: build and test VDX from a clean checkout with the README commands.
#
#   tools/clean_checkout.sh [repo-url-or-path] [work-dir]
#
# Defaults: this repository (committed HEAD) and a new temporary directory.
# Steps (README "Build"): desktop configure/build/ctest, the Emscripten core
# tests, the web module, and the UI checks. Emscripten steps run only when
# emcc is on PATH. Exit status is non-zero on the first failure.
set -euo pipefail
src="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
work="${2:-$(mktemp -d -t vdx-clean)}"
echo "AC-B1 clean checkout: $src -> $work"
git clone --recursive --quiet "$src" "$work/vdx"
cd "$work/vdx"

gen=()
command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)
arch=()
[ "$(uname)" = "Darwin" ] && arch=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")

echo "== desktop: configure, build, test"
cmake -S . -B build "${gen[@]}" "${arch[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

if command -v emcc >/dev/null 2>&1; then
  echo "== wasm: core tests under Node"
  emcmake cmake -S . -B build-wasm "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DVDX_BUILD_TESTS=ON
  cmake --build build-wasm --parallel
  ctest --test-dir build-wasm --output-on-failure
  echo "== web module"
  web/build.sh
else
  echo "== emcc not on PATH: skipping the Emscripten steps"
fi

echo "== ui"
(cd ui && npm ci && npm run check && npm test && npm run contrast && npm run build && npm run raster-check)
echo "AC-B1: all steps passed in $work/vdx"

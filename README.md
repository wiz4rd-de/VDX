# VDX

A 6-operator FM synthesizer that reproduces the sound and front-panel workflow
of a first-generation 6-operator FM instrument — 32 buttons, a 16×2 LCD, a
2-digit LED, data entry, voice and bank SysEx — plus a "programmer" view that
shows every parameter at once. One C++17 engine (`vdx_core`) and one Svelte UI
run in three builds: a standalone app, a VST3/AU plugin and a locally hosted web
page. SPEC.md is the product specification; DECISIONS.md records the choices
made where it left room.

## Install a release

[GitHub Releases](https://github.com/wiz4rd-de/VDX/releases) has packages for
macOS, Windows and Linux. They are **not code-signed**, so the operating
system warns the first time.

- **macOS 13 or later (Apple silicon and Intel):** open `VDX-<version>-macOS.pkg`.
  macOS refuses it the first time: open System Settings → Privacy & Security,
  click "Open Anyway" next to the message about the package, and confirm. It
  puts `VDX.app` into /Applications and the VST3 and AU plugins into
  /Library/Audio/Plug-Ins; "Customize" lets you leave parts out.
- **Windows 10 22H2 or later / Windows 11 (x64):** run
  `VDX-<version>-Windows-x64-Setup.exe`. If SmartScreen says "Windows protected
  your PC", choose "More info", then "Run anyway". It puts the app into
  Program Files\VDX and the VST3 plugin into Common Files\VST3. The zip holds
  the same files to copy by hand. The interface needs the Microsoft Edge
  WebView2 Runtime: Windows 11 includes it; on Windows 10,
  [install it](https://developer.microsoft.com/microsoft-edge/webview2/) if VDX
  says it is missing.
- **Linux (Ubuntu 24.04, x64):** unpack `VDX-<version>-Linux-x64.tar.gz`,
  install WebKitGTK (`sudo apt install libwebkit2gtk-4.1-0`), run `./VDX`, and
  copy `VDX.vst3` into `~/.vst3` for the plugin. `INSTALL.txt` has the details.

`SHA256SUMS` lists the checksums. Every package carries `NOTICES.txt` with the
licences of VDX and of the third-party code built into it.

## Requirements

| Build | Tools |
| --- | --- |
| Desktop (all) | CMake ≥ 3.25, a C++20 compiler (Xcode 15+, MSVC 2022, GCC 13), Node.js ≥ 22 + npm (the UI bundle is built during the CMake build), Python 3 (tests) |
| Ubuntu 24.04 | `sudo apt install ninja-build libwebkit2gtk-4.1-dev libgtk-3-dev libasound2-dev libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev` |
| Windows | Visual Studio 2022; the WebView2 SDK: `nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory %USERPROFILE%\AppData\Local\PackageManagement\NuGet\Packages` |
| WebAssembly / web | Emscripten SDK 6.0.13 (`./emsdk install 6.0.13 && ./emsdk activate 6.0.13 && source ./emsdk_env.sh`), Node.js 26 |

Clone with submodules (JUCE 9.0.5):

```sh
git clone --recursive <repo> vdx && cd vdx
```

## Desktop: configure, build, test

Options: `VDX_BUILD_PLUGIN` (default ON: standalone, VST3, AU on macOS),
`VDX_BUILD_TESTS` (default ON), `VDX_BUILD_WEB` (Emscripten only; use `web/build.sh`).

```sh
# macOS (universal binary)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# Ubuntu
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
# Windows (Developer PowerShell)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure      # Linux: prefix with xvfb-run -a
```

Artefacts are in `build/plugin/VDX_artefacts/Release/{Standalone,VST3,AU}`.
The VST3 and AU are also copied to the plug-in folders after the build (on
Windows the system VST3 folder needs an elevated prompt). The plugin build runs
`npm ci` (first time) and `npm run build` in `ui/` and embeds `ui/dist`, so Node.js
and npm must be on PATH whenever `VDX_BUILD_PLUGIN=ON`.

Plugin validation (pluginval is a separate download, never part of the repo):
`pluginval --strictness-level 5 --validate build/plugin/VDX_artefacts/Release/VST3/VDX.vst3`;
on macOS also `auval -v aumu Vdxf Mfnk` (run `killall -9 AudioComponentRegistrar`
after the first install). The standalone keeps its state in
`~/Library/Application Support/VDX/`, `%APPDATA%\VDX\` or `~/.config/VDX/`
(`VDX_STATE_DIR` overrides it).
Core-only build without JUCE: add `-DVDX_BUILD_PLUGIN=OFF`.

Useful targets: `vdx_tests` (Catch2 core suite: `./build/tests/vdx_tests "[tier1]"`),
`vdx_bench` (§9.1 benchmark: `./build/tools/bench/vdx_bench`).

## WebAssembly: core tests under Node

The same Catch2 binary, compiled with Emscripten and run with Node:

```sh
source <emsdk>/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DVDX_BUILD_TESTS=ON
cmake --build build-wasm --parallel
ctest --test-dir build-wasm --output-on-failure
node build-wasm/tools/bench/vdx_bench.js
```

## Web page (`npm run web`)

```sh
source <emsdk>/emsdk_env.sh
web/build.sh                  # -> ui/public/vdx.js + vdx.wasm (Wasm Audio Worklets)
cd ui && npm ci
npm run web                   # Vite on http://localhost:5173 with COOP/COEP
```

Open http://localhost:5173 in Chrome or Edge (Web MIDI) or Safari 17+
(computer keyboard only), click or press a key to start. `npm run web` builds
the WASM module first when it is missing and `emcc` is available.

UI checks: `npm run check`, `npm test` (Vitest), `npm run contrast`,
`npm run build && npm run raster-check`, `npm run e2e` (Playwright, Chromium + WebKit).

## Repository layout

```
core/            vdx_core: engine (core/fm), SysEx/state (core/sysex), front panel (core/panel), Instrument
bridge/          JSON UI protocol shared by the desktop editor and the WASM module (docs/PROTOCOL.md)
plugin/          JUCE processor, editor (WebBrowserComponent), standalone app, plugin tests
web/             worklet.cpp + build.sh (browser module)
ui/              Svelte 5 + TypeScript UI (one bundle for every build)
tests/           Catch2 suites (tier1 engine, tier2 panel/data) and goldens
banks/           default bank sources (JSON) and banks/vdx-default.syx
tools/           table generator, default-bank generator, licence check, benchmark, AC coverage, packaging
docs/            LICENSES.md, STATE.md (state blob layout), PROTOCOL.md
```

`tools/fetch_banks.sh` downloads community banks into the git-ignored
`banks/external/` for local round-trip testing. `python3 tools/ac_coverage.py`
lists every acceptance criterion and the tests that cover it.

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(VDX VERSION …)`) and
   `plugin/CMakeLists.txt` (`VERSION`), and merge that to `main` through a PR.
2. Run the Release workflow on `main`: Actions → Release → Run workflow, or
   `gh workflow run release.yml -f version=X.Y.Z` (add `-f prerelease=false`
   for a full release).
3. It builds the three platforms, packages them with `tools/package/`, checks
   the packaged plugins with pluginval and creates a draft release with the
   packages and `SHA256SUMS`. Check the draft, then publish it.

Signing runs only when the signing secrets exist (`tools/sign/`); without them
the packages are unsigned. DECISIONS.md D-PL16–D-PL19 describe the packages.

## Licences

VDX's own code is released under the [MIT License](LICENSE). Third-party
components keep their own licences; [docs/LICENSES.md](docs/LICENSES.md) lists
them. This includes the MSFA-derived engine files, which stay Apache-2.0 (see
`core/fm/third_party/msfa/NOTICE`).

JUCE is not covered by the MIT License. It is a git submodule pointing at the
upstream JUCE repository, and the plugin and standalone builds use it under the
JUCE 9 Starter licence. Anyone who builds or distributes those builds must use
JUCE under its own terms: one of the JUCE 9 EULA tiers, or the AGPLv3. The core
library, its Catch2 suite and the web build do not use JUCE.

No copyleft code is linked or shipped (`python3 tools/licence_check.py --self-test`).

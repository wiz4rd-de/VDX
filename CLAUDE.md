# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

VDX is a 6-operator FM synthesizer: one C++17 engine/front-panel library (`vdx_core`) and one Svelte 5 + TypeScript UI, shipped as three builds — JUCE standalone app, VST3/AU plugin, and a locally hosted web page (Emscripten + AudioWorklet).

Source-of-truth documents:
- **SPEC.md** is the product spec. Requirement IDs (`R-E4`, `F-12`, `AC-E1.4`, …) are cited throughout the code and tests. §13 holds the acceptance criteria, §9.4 the binding technical constraints, §14 what is left to the implementer.
- **DECISIONS.md** records every choice made where the spec left room or contradicted itself (`D-E*`, `D-P*`, `D-PL*`, `D-U*`, `D-W*`, grouped by area). Read the relevant section before changing behaviour. Add new decisions there in the same format (what, why, spec section).
- **KICKOFF.md** holds the working rules. The engine formulas in §7.1 and the LCD strings in §5.3 are exact data. When the spec conflicts with itself, the more specific text wins (acceptance criterion > rule > table example), and the conflict goes into DECISIONS.md. Build no features beyond the spec.
- **docs/PROTOCOL.md** describes the UI ⇄ core JSON protocol and **docs/STATE.md** the state blob layout. Update them when you change either.

## Commands

### Desktop (CMake ≥ 3.25, Ninja)
Clone with `--recursive`: JUCE is a submodule in `third_party/JUCE`.

```sh
# Core only (fast; no JUCE, no Node needed)
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DVDX_BUILD_PLUGIN=OFF
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure          # Linux: xvfb-run -a ctest ...

# With plugin + standalone (runs npm ci/build in ui/ and embeds ui/dist)
cmake -S . -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release [-DVDX_PLUGINVAL=/path/to/pluginval]
cmake --build build/plugin --parallel
```

The README uses a single `build/` directory. The local trees in use are `build/dev` (core only) and `build/plugin`. `web/build.sh` writes to `build/web`.

Running Catch2 tests directly (`vdx_tests` = core suite; `vdx_plugin_tests` = headless JUCE processor):
```sh
./build/dev/tests/vdx_tests "[tier1]"            # by tag: tier1, panel, midi, sysex, state, instrument, golden, smoke, ...
./build/dev/tests/vdx_tests "AC-E4.1*"           # by name — every AC test name starts with its AC ID
VDX_UPDATE_GOLDENS=1 ./build/dev/tests/vdx_tests "AC-E1.4*"   # rewrite tests/golden/dac_taps.sha256 (only for an intended sound change)
ctest --test-dir build/dev -L licence            # labelled ctest groups: licence, bench
./build/dev/tools/bench/vdx_bench --limit 5      # AC-N1.1 benchmark
```
Tier-2 tests are not tagged `[tier2]`. Filter them by area tag or by AC ID.

### Generators and checks (all also run as CTest entries)
```sh
python3 tools/gen_tables.py [--check]            # core/fm/tables/*.inc (AC-E3.2)
python3 tools/make_default_bank.py [--check]     # banks/default/*.json -> banks/vdx-default.syx + core/sysex/default_bank.inc (AC-D7.1)
python3 tools/licence_check.py --self-test | --branding | --licenses
python3 tools/ac_coverage.py [--missing]         # which tests name each §13 AC
tools/clean_checkout.sh                          # AC-B1: README build from a clean clone
```

### WebAssembly (Emscripten SDK 6.0.13, `source <emsdk>/emsdk_env.sh`)
```sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DVDX_BUILD_TESTS=ON
cmake --build build-wasm --parallel && ctest --test-dir build-wasm --output-on-failure   # same Catch2 binary under Node
web/build.sh                                     # -> ui/public/vdx.{js,wasm,aw.js}
```

### UI (`cd ui`, Node ≥ 22)
```sh
npm ci
npm run web            # Vite on :5173 with COOP/COEP; builds the WASM module only if ui/public/vdx.wasm is missing
npm run check          # svelte-check, fails on warnings
npm test               # Vitest; single file: npx vitest run tests/logic.test.ts -t "params.h"
npm run contrast       # AC-U6.1
npm run build && npm run raster-check
npm run e2e            # Playwright, Chromium + WebKit; starts web:e2e (:5173) and web:noiso (:5174) itself
npx playwright test e2e/bank.spec.ts --project=chromium -g "AC-F12.2"
```

### Full local gate set (before any PR)
`ctest` on a plugin-enabled build (core + plugin tests, licence/generator checks, benchmark; pluginval when `VDX_PLUGINVAL` is set), the Emscripten `ctest`, then in `ui/`: `check`, `test`, `contrast`, `build` + `raster-check`, `e2e`. CI (`.github/workflows/ci.yml`) runs the same set only on PRs into `main` and on manual dispatch. `smoke.yml` runs only `vdx_tests "[smoke]"` after a merge to `main`.

### Releases
`.github/workflows/release.yml` runs only by manual dispatch on `main` (`gh workflow run release.yml -f version=X.Y.Z`; the version must equal both CMake versions). It builds, writes `NOTICES.txt` into the bundles, signs only if the secrets exist, packages with `tools/package/` (macOS `.pkg`, Windows Inno Setup + zip, Linux `.tar.gz`), runs pluginval on the packaged plugins and creates a draft GitHub Release. Packages are unsigned (D-PL17). Test packaging locally on a copy of the artefacts: `python3 tools/package/notices.py --install <copy>` then `tools/package/macos.sh <copy> <version> <out>`.

## Architecture

### Layers
```
ui/ (Svelte, one bundle)  ──JSON──  bridge/ (vdx_bridge)  ──Command/Event──  vdx::Instrument  ──  vdx::Core
        ▲                                   ▲                                    (core/instrument.cpp)  (core/core.cpp + core/panel, core/sysex, core/fm)
        │                                   │
  JUCE WebBrowserComponent         plugin/src/editor.cpp (message thread)
  or WASM main thread              web/worklet.cpp (main thread; render in AudioWorklet)
```

- **`vdx::Core`** (`core/core.h`, implementation state in `core/panel/core_impl.h`) owns all state: edit buffer, banks, function params, panel globals, front-panel state machine. It is single-threaded and touched only from the audio thread. `Core::Impl`'s member functions are spread over `core/core.cpp` (dispatch, state blob), `core/panel/panel.cpp` (front-panel flows), `lcd.cpp` (LCD/LED/lamps), `midi.cpp` (receive, SysEx, transmit), and `refresh.cpp` (engine sync, event emission, timers, snapshot). Timers (splash, messages, YES/NO auto-repeat, transmit throttle) count engine samples, not wall time.
- **`vdx::Instrument`** (`core/include/vdx/vdx.h`, signatures binding per SPEC §9.4) wraps Core with lock-free SPSC queues (`core/util/spsc.h`). Other threads `post(Command)` and `pollEvent(Event)`; the audio thread calls `midi()` and `render()`. It schedules host-timestamped MIDI onto 64-sample internal blocks at 49,096 Hz and resamples to the host rate with r8brain. Its header comment explains the timing model (results are independent of host block size).
- **Commands and events** are fixed-size PODs (`CmdType` / `EvType` in `vdx.h`). Every change carries a `Source` (Panel, Programmer, Host, SysEx, Midi, …). The source drives MIDI transmit, host-parameter reporting and the panel's mode-switch rules, so pass the right one.
- **`vdx_bridge`** (`bridge/`) translates JSON ⇄ Command/Event. The desktop editor and the WASM module share it, so encoding is identical in every build. Shell-only commands (`pickFile`, `openDevices`, `midiSelect`, …) are not handled by the bridge.
- **UI**: `ui/src/bridge.ts` is the only path to the core, with backends `juce` | `wasm` | `test`. `store.svelte.ts` is the single store that applies event batches; values show only after `snapshotEnd`. Display strings always come from the core (`s` fields); the UI never formats parameter values itself. `ui/src/web/` is the web-only glue (start card, IndexedDB, Web MIDI).
- **Plugin** (`plugin/src`): `processor.cpp` (audio thread, host-parameter mirror of 174 params, programs), `host_params.cpp` (IDs/value strings), `editor.cpp` (web view, resource provider serving the embedded `ui/dist`), `standalone_app.cpp`, `shell/persistence.cpp` (standalone `state.bin` + `internal.syx`). Host round-trip/echo handling is described in DECISIONS D-PL3/D-PL4.
- **Engine** (`core/fm`): Mk I and Modern kernels over vendored MSFA (`core/fm/third_party/msfa`, Apache-2.0, only the files listed in `core/CMakeLists.txt` are compiled). Envelopes, LFO/pitch EG, DAC stage + SVF.
- **Data** (`core/sysex`): unpacked/packed voices with a raw packed copy (byte-identical round trips), SysEx parse/build, file scanning, the `VDX1` state blob with CRC32.

### Invariants that tests enforce
- **Real-time safety** in `render()` / `midi()` and everything Core does: no allocation, locks, logging or exceptions, fixed arrays only. AC-E15.2 checks this with `tests/support/rt_hooks.cpp`.
- **Bit-exact determinism** across macOS, Windows, Linux and WASM. AC-E1.4 compares SHA-256 hashes of 59 DAC-tap renders. Builds use strict FP (`cmake/VdxFlags.cmake`: `-ffp-contract=off`, `/fp:precise`), and the signal path avoids libm: use `core/fm/detmath.h` instead of `exp`/`log`/`pow`.
- **Mirrors that must stay in sync** (Vitest checks them): `ui/src/params.ts` ↔ `core/include/vdx/params.h`; `ui/src/programmer/algorithms.ts` ↔ `FmCore::algorithms` in MSFA's `fm_core.cc`. Generated files have `--check` CTests (tables, default bank).
- **Test naming**: every §13 acceptance criterion has a test whose name contains its AC ID (Catch2 `TEST_CASE`, CTest name, Vitest `it`, Playwright `test`). `tools/ac_coverage.py` finds them by that ID.
- **Licensing/branding**: no copyleft code is linked or vendored (pluginval is downloaded, never committed). "YAMAHA" and "DX7" must not appear in `ui/src`, `plugin/` or built plugin metadata (`licence_check.py --branding`). The one exception is the "DX7 SysEx files" file-filter text. New third-party components need an entry in `docs/LICENSES.md`.

### Gotchas
- After changing C++ that the web build uses, rerun `web/build.sh`. `npm run web`/`e2e` keep a stale `ui/public/vdx.wasm` if one exists.
- The plugin build embeds `ui/dist` and whatever is in `ui/public` at that moment (`tools/embed_ui.py`). It also copies the VST3/AU into the user plug-in folders (`COPY_PLUGIN_AFTER_BUILD`). The AU pluginval test validates the installed `~/Library/Audio/Plug-Ins/Components/VDX.component`.
- Playwright screenshot baselines (AC-SC1) are per platform: `*-chromium-darwin.png` locally, and `*-chromium-linux.png` from the `mcr.microsoft.com/playwright:v1.64.1-noble` image. Tolerance is 0.1 % of pixels.
- `npm run web:e2e` sets `VITE_VDX_TEST=1` (`ui/.env.e2e`), which exposes the analyser tap `window.__vdxTap`. Test doubles for Web MIDI, AudioContext and IndexedDB are in `ui/e2e/support/doubles.ts`.
- The standalone's state directory can be redirected with `VDX_STATE_DIR`.

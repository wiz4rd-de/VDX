# VDX — Implementation plan

Vertical slices in the order recommended by SPEC §14. Each slice ends with: all tests green, commit.
IDs in brackets are the spec items the slice satisfies.

## S0 — Repository, build, CI, licensing
- [x] Layout per §14; JUCE 9.0.5 submodule; vendored r8brain (MIT), MSFA from Dexed `source/msfa` minus `tuning.*` (Apache-2.0 + NOTICE), Catch2 v3 amalgamated (BSL).
- [ ] Root CMake (≥ 3.25) with `VDX_BUILD_PLUGIN`, `VDX_BUILD_WEB`, `VDX_BUILD_TESTS`; deterministic FP flags (`-ffp-contract=off`, `/fp:precise`).
- [ ] `tools/licence_check.py` + its self-test [R-L1–R-L3, AC-L1.1]; `docs/LICENSES.md` [R-L4, AC-L4.1]; branding grep [R-L5, AC-L5.1].
- [ ] CI workflow (PR into `main` + `workflow_dispatch` only; post-merge smoke on `main`): macOS universal, Windows MSVC, Ubuntu 24.04 + webkit2gtk-4.1, wasm; runs every §13 suite, licence check, benchmarks; signing steps gated on secrets [§15.1, AC-B1].

## S1 — Sound engine (`core/fm`) [R-E1–R-E15]
- [ ] Generated integer tables committed (`tools/gen_tables.py`, `core/fm/tables/*.inc`) + regeneration test [R-E3, AC-E3.2].
- [ ] Deterministic math (`detmath.h`: exp/exp2/log without libm) for detune, AM, portamento tables.
- [ ] Mk I kernel, Modern kernel (MSFA `fm_op_kernel`), algorithm routing incl. chain feedback for 4/6, carrier attenuation [R-E3, R-E8, R-E14, AC-E3.1, AC-E3.3, AC-E8.1, AC-E8.2, AC-E1.5].
- [ ] Per-sample envelope with gate masks, holds, attack jump [R-E4, AC-E4.1–E4.4].
- [ ] Levels, velocity, scaling [R-E5, AC-E5.1–E5.3]; frequency incl. Mk I quantisation, detune, fixed mode, master tune, bend/step [R-E6, AC-E6.1–E6.4].
- [ ] LFO (MSFA), pitch EG (MSFA), sensitivities [R-E7, AC-E7.1–E7.3].
- [ ] DAC stage + SVF, output stage [R-E9, R-E10, AC-E9.1, AC-E10.1].
- [ ] 16-voice allocation, sustain [R-E11, AC-E11.1, AC-E11.2]; mono/porta [R-E12, AC-E12.1–E12.3]; controllers [R-E13, AC-E13.1].
- [ ] Instrument pipeline: lock-free queues, host-time MIDI scheduling to internal 64-sample blocks, r8brain resampler, latency, host-rate mode, block chunking, denormal dither [R-E1, R-E15, AC-E1.1, AC-E1.2, AC-E14.1, AC-E15.1–E15.3].

## S2 — Data: voices, SysEx, files, state (`core/sysex`) [§4, R-D1–R-D6]
- [ ] Unpacked/packed voice with raw packed copy; effective values [§4.1, §4.6, R-D3].
- [ ] SysEx build/parse, checksum, filters [§4.7, F-13]; file scan [R-D4, AC-D4.1]; round trip [AC-D3.1].
- [ ] Default bank: `banks/default/*.json` → `tools/make_default_bank.py` → `banks/vdx-default.syx` → embedded [§15.3, R-P10, AC-P10.1, AC-D7.1].
- [ ] State blob `VDX1` + CRC32, newer/older version handling, `docs/STATE.md` [§4.8, AC-D1.1, AC-D1.2, AC-D6.1].

## S3 — Front panel and flows (`core/panel`) [§5.3, §5.4, §6, R-P1–R-P11, R-M1–R-M3, R-S1–R-S4]
- [ ] Panel modes/pages/sub-pages, dialogs, messages, splash, timers on the audio clock, YES/NO auto-repeat [F-1, F-2, AC-F2.1, AC-F2.2, AC-F1.1 core part].
- [ ] LCD text per §5.3, LED, lamps; hand-written LCD golden matrix [R-P2, AC-P2.1, AC-P1.1 core golden].
- [ ] Data entry, YES/NO, CC6/96/97 [R-P3, AC-P3.1]; EDITED/CLEAN, recall, mask, protect, INIT VOICE, stored voice [R-P4–R-P11, AC-P4.1–AC-P11.1].
- [ ] Flows F-3…F-14 [AC-F3.1–AC-F14.1]; MIDI receive/transmit, stream robustness [AC-M1.1–AC-M3.1, AC-F13.1].
- [ ] Four edit paths + external-change rules [R-S1, R-S2, AC-S1.1 (core paths), AC-S2.1, AC-F9.1].

## S4 — Goldens, WASM tests, benchmark [AC-E1.4, §13 WASM runs, §9.1]
- [ ] `tools/golden` renderer, committed golden hashes (59 renders) [AC-E1.4].
- [ ] Emscripten build of the Catch2 binary run under Node 26 (identical expectations).
- [ ] `tools/bench` native + WASM, < 5 % / < 25 % real time, µs per 128-sample block [AC-N1.1].

## S5 — Bridge + metadata
- [ ] `vdx_bridge`: JSON ⇄ Command/Event (shared by desktop and WASM), parameter metadata export to `ui/src/generated/params.ts`.

## S6 — Plugin and standalone (`plugin/`, JUCE 9) [§4.9, §5.1, §5.10, §9.3, F-15, R-D2]
- [ ] `juce_add_plugin` with the §9.3 identity; 174 host params, value strings, 32 programs [AC-PL2].
- [ ] Host round trip (audio-thread mirror) [R-S4, AC-S4.1]; host automation path of AC-S1.1; state get/set [AC-PL3]; omni/channel in plugin [AC-M1.1].
- [ ] WebBrowserComponent editor + resource provider + native functions; WebView fallback label [§5.10]; zoom/constrainer [§5.1]; keyboard default off [AC-K2.2].
- [ ] Standalone persistence (`state.bin` + `internal.syx`, temp+rename, timing) [F-15, R-D2, AC-F15.1]; device selector; idle pump when no audio device.
- [ ] pluginval strictness 5 (VST3 + AU locally on macOS) [AC-PL1].

## S7 — UI (`ui/`, Svelte 5 + TS) [§5, §11, R-K1, R-K2, R-U1–R-U6]
- [ ] Canvas scaling, theme tokens, bundled fonts, SVG LCD (`font5x7.ts`) and LED (`segments.ts`).
- [ ] Front panel, programmer blocks, operator panels, voice menu, context menu, bank manager, settings.
- [ ] `keyboard.ts` + Vitest [AC-K1.1, AC-K2.1]; contrast script [AC-U6.1]; raster check [AC-SC3].

## S8 — Web build and end-to-end tests [§5.9, §15.2, R-D5]
- [ ] `web/worklet.cpp`, `web/build.sh` (Wasm Audio Worklets), Vite on :5173 with COOP/COEP (dev + preview).
- [ ] Web glue: start card, error screens, IndexedDB, Web MIDI (+ test double), analyser tap in test builds.
- [ ] Playwright (Chromium + WebKit): AC-B2, AC-E1.3, AC-F1.1, AC-F12.1, AC-F12.2, AC-F15.2, AC-S5.1, AC-S6.1, AC-SC1, AC-SC2, AC-U1.1–AC-U5.1, AC-P1.1 lamps, AC-K2.2 (plugin flag).

## S9 — Final verification
- [ ] Full suites + benchmarks, Linux run in Docker, exercise every §6 flow in the standalone and `npm run web`, fix, REPORT.md.

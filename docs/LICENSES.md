# Licences

VDX is a personal project owned by Marco Funk. Nothing under a copyleft licence
is linked into or shipped with VDX (SPEC R-L1, R-L2); `tools/licence_check.py`
enforces it in CI (R-L3).

## Shipped components

| Component | Licence | Where | Notes |
| --- | --- | --- | --- |
| JUCE 9.0.5 | JUCE 9 Starter EULA (free while revenue/funding over the previous 12 months is at most $20,000; otherwise Indie or relicensing VDX, see SPEC R-L1) | `third_party/JUCE` (git submodule, unmodified) | Standalone, VST3 and AU shells |
| VST3 SDK | MIT | via JUCE | VST3 format |
| MSFA (Music Synthesizer for Android, as distributed in Dexed `source/msfa`, without `tuning.cc/.h`) | Apache-2.0, with NOTICE (`core/fm/third_party/msfa/NOTICE`) | `core/fm/third_party/msfa/`; derived files `core/fm/dx7tables.h`, `core/fm/lfo_pitchenv.h`, parts of `core/fm/engine.cpp` | Engine tables, Modern kernel, LFO, pitch EG, detune and portamento formulas |
| r8brain-free-src | MIT | `third_party/r8brain` | Resampler (Ooura FFT `fft4g.h`, same licence terms) |
| Svelte 5 | MIT | UI bundle (`ui/dist`) | UI framework |
| Barlow Condensed (500, 600) | SIL OFL 1.1 | `ui/src/fonts` | Legends and numbers |
| Oswald (500) | SIL OFL 1.1 | `ui/src/fonts` | Wordmark |

## Build and test tools (not shipped)

| Tool | Licence | Use |
| --- | --- | --- |
| CMake, Ninja | BSD-3-Clause / Apache-2.0 | Build |
| Emscripten | MIT / University of Illinois NCSA | WebAssembly build |
| Vite, Vitest, svelte-check, TypeScript | MIT / MIT / MIT / Apache-2.0 | UI build and unit tests |
| Playwright | Apache-2.0 | End-to-end tests |
| axe-core (via @axe-core/playwright) | MPL-2.0 | Accessibility test (AC-U5.1) |
| Catch2 v3.8.1 | BSL-1.0 | Native and WASM test runner (`third_party/catch2`, tests only) |
| pluginval | GPL-3.0 | Downloaded by CI as a separate executable; never in the repository, never linked |
| Dexed (oracle job only) | GPL-3.0 | Optional `tools/oracle` report; fetched and built into the git-ignored `tools/oracle/build/`, never linked or shipped |

## Reference material (no code copied)

hexter (GPL-2.0+) — measured numbers only. ajxs DX7 v1.8 ROM disassembly
(no licence; Yamaha firmware) — behaviour and short UI strings only.

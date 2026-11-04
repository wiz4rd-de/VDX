# VDX UI ⇄ core protocol

The UI (`ui/`, Svelte) is one bundle for every build. It talks to the core only
through `ui/src/bridge.ts`, which has two backends:

| Backend | Transport | Core host |
| --- | --- | --- |
| `juce` (standalone, plugin) | `window.__JUCE__.backend`: native function `vdxCommand(json)`, event `vdxEvents` (a JSON array, batched at ≤ 60 Hz) | `vdx::Instrument` in the JUCE processor |
| `wasm` (web) | direct calls into the WebAssembly module on the main thread (`vdx_command(json)`, `vdx_poll(): json`) | `vdx::Instrument` rendering in an AudioWorklet (shared memory) |

Both backends use the same JSON, produced and parsed by the C++ library
`vdx_bridge` (`bridge/`), so encoding is identical in every build.

## Commands (UI → core)

Every message is an object with a `t` field. Unknown messages are ignored.

| `t` | Fields | Meaning |
| --- | --- | --- |
| `noteOn` | `note`, `vel` | Note on (computer keyboard). `src` is Keyboard. |
| `noteOff` | `note` | Note off. |
| `cc` | `cc`, `v` | Controller from the UI: 1 mod wheel, 64 sustain, … |
| `bend` | `v` 0…16383 | On-screen pitch wheel / keyboard bend. |
| `releaseAll` | | Release every keyboard note, centre the pitch wheel. |
| `button` | `id` 1…42, `down` bool | Front-panel button. 1–32 numbered; 33 MEMORY SELECT INTERNAL, 34 MEMORY SELECT CARTRIDGE, 35 EDIT/COMPARE, 36 FUNCTION, 37 STORE, 38 MEMORY PROTECT INTERNAL, 39 MEMORY PROTECT CARTRIDGE, 40 OPERATOR SELECT, 41 NO/−1, 42 YES/+1. Send `down:true` on press and `down:false` on release (YES/NO auto-repeat is done by the core). |
| `dataEntry` | `pos` 0…1 | Data-entry slider position. |
| `setParam` | `i` 0…155, `v` | Programmer change of a voice parameter. |
| `setFunc` | `i` 64…77, `v` | Programmer change of a function parameter. |
| `setGlobal` | `id`, `v` | `id` ∈ `masterTune`, `midiChannel`, `omni`, `sysInfo`, `protectInt`, `protectCrt`, `volume`. |
| `setSetting` | `id`, `v` | `id` ∈ `engine` (0 Mk I, 1 Modern), `dac`, `hostRate`, `classicVelocity`, `keyboardPlay`, `zoom` (50…200), `opOrder` (0 OP1→OP6, 1 OP6→OP1). |
| `writeOp` | `op` 1…6, `bytes` [21] | Paste / init operator (R-U2). |
| `swapOps` | `a`, `b` | Swap two operators' 21 bytes. |
| `egCopy` | `from`, `to` | Copy R1–R4/L1–L4. |
| `initVoice` | | Voice menu: Init voice. |
| `randomise` | `seed` | Voice menu: Randomise (UI passes `Date.now()` & 0x7fffffff; tests pass 1). |
| `compare` | | Voice menu: Compare. |
| `selectVoice` | `bank` 0/1, `slot` 1…32 | Bank manager double-click / drop on the edit buffer. |
| `copyVoice` | `fromBank`, `fromSlot`, `toBank`, `toSlot` | Bank manager row drop. |
| `newCartridge` | | |
| `ejectCartridge` | | |
| `export` | `what` ∈ `int`, `crt`, `voice` | Export… (the shell writes the file / triggers the download). |
| `sendBank` | | Send bank (MIDI). |
| `requestDump` | | Request dump (MIDI). |
| `importFile` | `name`, `b64` | A chosen or dropped file (base64). |
| `importChoice` | `target` ∈ `crt`, `int` | Answer to a bank import choice. |
| `importCancel` | | Cancel the bank import choice. |
| `snapshot` | | Request the complete state (R-S5). Sent once when the UI connects. |
| `startSplash` | | Start the power-on splash (sent by the web glue on the start gesture; the desktop shell posts it natively). |
| `exportDone` | `what` | The shell wrote an export; for `crt` this clears the cartridge's changed flag (F-12.5). |

Shell-only commands (handled by the desktop shell or the web glue, never by the core):

| `t` | Build | Meaning |
| --- | --- | --- |
| `pickFile` | all | Open the OS / browser file picker for `.syx`; the shell then sends `importFile` itself. |
| `openDevices` | standalone | Open JUCE's audio/MIDI device selector. |
| `midiSelect` | web | `inputs`: [ids], `output`: id or null. |
| `uiReady` | all | The UI has mounted (the shell may start the splash). |
| `editorClosing` | plugin/standalone | Sent from `beforeunload`. |

## Events (core → UI)

Events arrive as arrays of objects with an `e` field, in order.

| `e` | Fields | Meaning |
| --- | --- | --- |
| `param` | `i`, `v`, `s`, `src` | Voice parameter 0…155 (effective value) and its display string `s` (identical to the LCD and the host value string, R-P8). |
| `func` | `i`, `v`, `s`, `src` | Function parameter 64…77. |
| `global` | `id`, `v`, `s` | Panel global (`s` e.g. master tune `+73.8 cents`). |
| `setting` | `id`, `v` | Setting value. |
| `lcd` | `l1`, `l2` (16 chars each), `row`, `col`, `cursor` bool, `lit` bool | LCD contents; the UI blinks the cursor cell every 400 ms. `lit:false` = all cells `--lcd-off`. |
| `led` | `v` 0…32 (0 = blank), `dot` | LED. |
| `lamps` | `num` (bitmask, bit i−1 = button i), `mode` (bitmask, bit k−33 = button k), `blink` (same layout as `mode`) | Lit buttons. Blinking lamps alternate 400 ms on / 400 ms off. |
| `panel` | `mode` (`PLAY`, `EDIT`, `EDIT_NAME`, `FUNCTION`, `SPLASH`), `page`, `op`, `comparing`, `edited`, `dialog`, `message`, `armed` | Panel state. |
| `dataEntry` | `pos` | Data-entry thumb position (F-2). |
| `wheels` | `bend`, `mod` | Wheel positions from received MIDI (the UI follows them when not dragging). |
| `voiceName` | `bank`, `slot`, `name` | A bank slot name (10 chars). |
| `cartridge` | `present`, `changed`, `name` | Cartridge status. |
| `memory` | `selected` (0/1), `srcBank`, `srcSlot`, `edited` | Selected memory and voice source. |
| `opFreq` | `op`, `s` | Computed ratio / frequency text per operator (as the LCD). |
| `lfoHz` | `s` | LFO speed in Hz, two decimals. |
| `meter` | `db` [6] (OP1…OP6, dBFS, ≥ −60) | Operator output meters (≤ 30 Hz). |
| `notice` | `code` (`protected`, `rejected`, `dumpReceived`, `completed`, `saveError`, `writeError`, `noReply`), `bank`, `name`, `text` | Inline notices for the bank manager / settings. |
| `import` | `kind` (`voice`, `bank`, `reject`), `name` | Result of `importFile`; for `bank` the UI shows the choice. |
| `snapshotEnd` | | End of a snapshot; the UI may now show values. |
| `env` | `build` (`web`, `standalone`, `plugin`), `midi` (`ok`, `unavailable`, `nosysex`), `inputs` [{id,name,on}], `outputs` [{id,name}], `output`, `idb` bool, `audio` bool | Shell environment (sent by the shell, not the core). |

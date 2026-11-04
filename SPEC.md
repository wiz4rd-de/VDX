# VDX — Product Spec

Nov 4, 2026 · Marco Funk

This is the single source of truth for building VDX. Sections 1–13 and 15 are decided. §14 lists what is deliberately left to the implementer. Anything this spec does not cover is handled by the rule in KICKOFF.md: choose the simplest behaviour consistent with the overview, glossary and existing rules, record it in `DECISIONS.md`, and continue.

---

## 1. Overview

VDX is a 6-operator FM synthesizer that reproduces the sound and the original front-panel workflow of a first-generation (Mk I) DX7: 32 numbered buttons, a 16×2 LCD, a 2-digit LED, data entry, and SysEx compatibility with DX7 voice and bank files. It also adds a modern "programmer" view that shows every parameter at once.

VDX runs in three builds:
- a standalone desktop app;
- a VST3/AU instrument plugin in a DAW;
- a locally hosted web page.

All three share one sound engine and one UI.

The core loop is: pick a voice → play it (MIDI keyboard, computer keyboard or DAW) → edit parameters on the front panel or in the programmer → store the voice into a bank slot → load and save banks as `.syx` files.

---

## 2. Glossary

Every term below is used with exactly this meaning in the rest of the spec.

| Term | Definition |
| --- | --- |
| **Voice** | One complete sound program: 155 bytes of parameters (indices 0–154, §4.1), including a 10-character name. "Patch" and "program" are not used except for the MIDI *program change* message and the host's *program* list. |
| **Operator (OP1…OP6)** | One of the six FM oscillator+envelope units. Always numbered 1–6 as on the hardware. Storage order in voice data is OP6 first (§4.1). Wherever an index appears in this spec, `op` = 1…6 means OPop. |
| **Operator mask** | 6 bits saying which operators sound (index 155, never stored in a voice). It resets to all-on whenever a voice is loaded into the edit buffer. |
| **Voice parameter** | One of indices 0–155: 0–154 are voice bytes, 155 is the operator mask. |
| **Function parameter** | One of the 14 performance settings, numbered 64–77 (§4.2). They are not part of a voice. |
| **Panel global** | Settings shown or set from the front panel that are neither voice nor function parameters (§4.3). |
| **Setting** | An option in the Settings overlay (§4.4). |
| **Edit buffer** | The voice that is currently sounding and being edited. |
| **Stored voice** | A copy of the slot contents taken when the edit buffer was last loaded from or stored to a slot. COMPARE plays it. |
| **Recall buffer** | A copy of an edited edit buffer, taken just before the edit buffer was replaced. EDIT RECALL restores it. |
| **Bank** | 32 voices in slots 1–32. |
| **Slot** | One position 1–32 in a bank. |
| **Internal bank** | The bank that is always present. |
| **Cartridge** | A second, optional bank held in memory. It is "inserted" by loading a 32-voice `.syx` file or by creating a new one in the bank manager, and "ejected" in the bank manager. It is saved in state with its name. It is written to disk **only** by an explicit Export. |
| **Selected memory** | INT or CRT: which bank the 32 buttons select from in PLAY. |
| **Voice source** | INT or CRT plus the slot the edit buffer was last loaded from or stored to. |
| **EDITED** | The edit buffer differs from its stored voice because of an edit. The opposite is **CLEAN**. |
| **Front panel** | The top strip of the UI (§5.2): wheels, sliders, LCD, LED and buttons. It reproduces the hardware. |
| **Programmer** | The lower part of the UI (§5.5–§5.6): all parameters as direct controls. |
| **Overlay** | The bank manager or Settings modal. |
| **Page** | What the LCD shows for the current button in EDIT or FUNCTION mode (§5.3). |
| **Panel mode** | PLAY, EDIT, EDIT_NAME or FUNCTION (§6.2). |
| **Dialog** | A pending confirmation or target prompt on the LCD (§6.2). |
| **Message** | A 2-second LCD notice (§5.3.5). |
| **Core** | The C++ library `vdx_core` that owns all synth and front-panel state (§9.4). |
| **Count** | Envelope level unit: 1/256 octave ≈ 0.0235 dB. |
| **Mk I mode / Modern mode** | The two engine modes (R-E12). |
| **Internal rate** | 49,096 Hz, the engine's native sample rate. |
| **Host** | The DAW running the plugin. |

---

## 3. Actors & access

- **One local user.** There are no accounts, no sign-in and no network features. VDX never makes a network request at runtime. The web build loads its files from localhost only.
- **Host (plugin build).** Automates the host parameters (§4.9), saves and restores state per plugin instance, and sends MIDI, including SysEx where the host supports it.
- **External MIDI devices and librarians.** They send notes, controllers, program changes and SysEx. They receive dumps and parameter changes only when SYS INFO AVAIL is on.
- **Several instances.** Plugin instances are fully independent. Two standalone processes or two browser tabs each run independently and share the same saved state. The last one to write wins (§8).

---

## 4. Data model

All byte layouts follow the 1993 DX7 SysEx document (also in Dexed's `Documentation/sysex-format.txt`).

### 4.1 Voice parameters (unpacked: 155 bytes + operator mask)

Each operator block is 21 bytes. The blocks sit at offsets 0 (OP6), 21 (OP5), 42 (OP4), 63 (OP3), 84 (OP2) and 105 (OP1):

| Offset | Parameter | Range |
| --- | --- | --- |
| 0–3 | EG rates R1–R4 | 0–99 |
| 4–7 | EG levels L1–L4 | 0–99 |
| 8 | Level-scaling break point | 0–99 (0 = A−1 = MIDI 21, 39 = C3 = MIDI 60, 99 = C8 = MIDI 120) |
| 9 / 10 | Left / right depth | 0–99 |
| 11 / 12 | Left / right curve | 0–3 (−LIN, −EXP, +EXP, +LIN) |
| 13 | Rate scaling | 0–7 |
| 14 | AM sensitivity | 0–3 |
| 15 | Key velocity sensitivity | 0–7 |
| 16 | Output level | 0–99 |
| 17 | Oscillator mode | 0 ratio, 1 fixed |
| 18 | Coarse | 0–31 |
| 19 | Fine | 0–99 |
| 20 | Detune | 0–14 (7 = 0) |

Global:

| Index | Parameter | Range |
| --- | --- | --- |
| 126–129 | Pitch EG rates 1–4 | 0–99 |
| 130–133 | Pitch EG levels 1–4 | 0–99 (50 = no shift) |
| 134 | Algorithm | 0–31 (shown 1–32) |
| 135 | Feedback | 0–7 |
| 136 | Oscillator key sync | 0–1 |
| 137 / 138 | LFO speed / delay | 0–99 |
| 139 / 140 | LFO PM depth / AM depth | 0–99 |
| 141 | LFO key sync | 0–1 |
| 142 | LFO wave | 0–5 (TRI, SAW DN, SAW UP, SQU, SIN, S/H) |
| 143 | Pitch mod sensitivity | 0–7 |
| 144 | Transpose | 0–48. 24 = middle C plays C3 (no transpose). The value is the note that middle C plays, counted from C1 = 0. |
| 145–154 | Name | 10 bytes, displayed as ASCII 0x20–0x7E. Any other byte is displayed and edited as a space but kept as-is in storage until the character is edited (R-D3). |
| 155 | Operator mask | 6 bits: **bit 5 = OP1 … bit 0 = OP6**. 0x3F = all on. Parameter change only; never stored. |

**Raw bytes and effective values.** The core keeps the 155 raw bytes exactly as loaded. Wherever a value is used (sound, LCD, UI, host parameters), its **effective value** is used: the raw value clamped to the range above. Editing a parameter writes an in-range value.

### 4.2 Function parameters (group 2, numbers 64–77)

| # | Parameter | Range | Default |
| --- | --- | --- | --- |
| 64 | Mono/poly | 0 poly, 1 mono | 0 |
| 65 | Pitch bend range | 0–12 semitones | 2 |
| 66 | Pitch bend step | 0–12 (step > 0 → range treated as 12) | 0 |
| 67 | Portamento mode | 0/1 = RETAIN/FOLLOW (poly), FINGERED/FULL TIME (mono) | 0 |
| 68 | Glissando | 0–1 | 0 |
| 69 | Portamento time | 0–99 (0 = no portamento) | 0 |
| 70 / 71 | Wheel range / assign | 0–99 / bits 0 pitch, 1 amp, 2 EG bias | 99 / 0b001 |
| 72 / 73 | Foot range / assign | same | 0 / 0 |
| 74 / 75 | Breath range / assign | same | 0 / 0 |
| 76 / 77 | Aftertouch range / assign | same | 0 / 0 |

The defaults apply on first run and after a state reset. VOICE INIT does not change function parameters.

### 4.3 Panel globals

Panel globals are not in SysEx. They are saved in state.

| Global | Range | Default |
| --- | --- | --- |
| Master tune | 0–127, 64 = 0 cents; cents = (v − 64) × 75/64 | 64 |
| MIDI receive channel | 1–16 (one value, shown on FUNCTION 8 and in Settings) | 1 |
| Omni | on/off | on |
| SYS INFO AVAIL | on/off | off |
| Memory protect internal / cartridge | on/off each | on / on (the saved value is restored on later launches) |
| Master volume | 0–127; gain = (v/127)², v = 0 is silence | 127 |

### 4.4 Settings

| Setting | Values | Default | Saved in |
| --- | --- | --- | --- |
| Engine | Mk I / Modern | Mk I | state |
| DAC emulation (Mk I only) | on/off | on | state |
| Render at host rate | on/off | off | state |
| Classic velocity scaling | on/off | off | state |
| Computer keyboard play | on/off | on (standalone, web), off (plugin) | state |
| Zoom | 50–200 % in steps of 10 | fit (§5.1) | state |
| Operator order in programmer | OP1→OP6 / OP6→OP1 | OP1→OP6 | state |
| Audio/MIDI devices | standalone: JUCE device selector; web: Web MIDI inputs/outputs | standalone: system default output, all MIDI inputs; web: all inputs, no output | standalone: JUCE standalone properties file; web: IndexedDB key `devices` |

### 4.5 Banks and buffers

- The internal bank always exists. On first run it holds the VDX default bank (§15.3).
- The cartridge is absent or present. When present, it has a name (≤ 64 characters):
  - loaded from a file: the file name without extension;
  - created with "New cartridge": `UNTITLED`.
- The cartridge also has a **changed** flag. It is set by any write to the cartridge and cleared by Export or by loading it from a file.
- The edit buffer, stored voice and recall buffer each hold one voice. On first run all three are slot 1 of the default bank (`INIT VOICE`), and the voice source is INT 1, CLEAN.

### 4.6 Packed 128-byte voice

Each operator takes 17 bytes, at offsets 0, 17, 34, 51, 68 and 85 (OP6 → OP1). Within each operator:
- bytes 0–10 are the same as unpacked bytes 0–10;
- byte 11 = `RC<<2 | LC`;
- byte 12 = `DET<<3 | RS`;
- byte 13 = `KVS<<2 | AMS`;
- byte 14 = OL;
- byte 15 = `COARSE<<1 | MODE`;
- byte 16 = fine.

Globals:

| Bytes | Content |
| --- | --- |
| 102–105 | PEG rates |
| 106–109 | PEG levels |
| 110 | Algorithm |
| 111 | `OKS<<3 \| FB` |
| 112–115 | LFO speed / delay / PMD / AMD |
| 116 | `PMS<<4 \| WAVE<<1 \| LKS` |
| 117 | Transpose |
| 118–127 | Name |

Unpacking keeps every bit that has a place in the unpacked form. Bits with no place in the unpacked form are kept in a per-voice 128-byte **raw packed copy**. Packing a voice that hasn't been edited since it was unpacked returns that raw copy unchanged. Packing an edited voice packs the current values, with unused bits = 0.

### 4.7 SysEx messages

| Message | Bytes | Notes |
| --- | --- | --- |
| Single voice (format 0) | `F0 43 0n 00 01 1B` + 155 + checksum + `F7` = 163 | n = device channel 0–15 |
| 32-voice bank (format 9) | `F0 43 0n 09 20 00` + 4096 packed + checksum + `F7` = 4104 | |
| Parameter change | `F0 43 1n 0gggggpp 0ppppppp 0ddddddd F7` | g = 0 voice: pp = 00 for parameters 0–127, pp = 01 for 128–155. g = 2 function: parameters 64–77. Other numbers are ignored. |
| Dump request | `F0 43 2n 00 F7` / `F0 43 2n 09 F7` | Answered with the edit buffer / internal bank |

- Checksum: `(−sum(data)) & 0x7F`.
- Only manufacturer 0x43 is accepted. Anything else is ignored.
- A message whose length doesn't match its format is ignored.
- On a checksum mismatch, the data loads anyway and the CHECK SUM ERROR! message is shown.

### 4.8 State

State contains:
- the internal bank (packed);
- the cartridge (present flag, packed data, name, changed flag);
- the edit buffer with its raw packed copy and its EDITED flag;
- the stored voice;
- the recall buffer;
- the voice source;
- the selected memory;
- the function parameters;
- the panel globals;
- the settings marked "state" in §4.4.

Panel mode, pages and dialogs are **not** saved. Every start shows the splash and then PLAY (F-1).

The blob begins with the magic `"VDX1"` and a u16 version (1), and ends with a CRC32 (IEEE 802.3) of everything before it. The field layout in between is latitude (§14), but must be documented in `docs/STATE.md`.

| Case | Behaviour |
| --- | --- |
| Blob from a newer version | Load the banks, buffers and function parameters; ignore the rest. |
| Bad magic, bad CRC, truncated blob, or unknown older version | Discard the whole blob, use first-run state, and show the message `  STATE RESET   ` after the splash. |
| Zero-length blob (a new plugin instance) | First-run state, no message. |

### 4.9 Host parameters (plugin)

| Group | Parameters | Type / range | ID |
| --- | --- | --- | --- |
| Voice | 145 voice parameters, indices 0–144 (all except the name) | `AudioParameterInt`, ranges as §4.1 | `op{1..6}_{name}`, `g_{name}` |
| Operator switches | 6 | `AudioParameterBool` | `op{n}_on` |
| Function, ranges and modes | 64–70, 72, 74, 76 | `AudioParameterInt` | |
| Function, assign bits | each assign byte (71/73/75/77) split into three | `AudioParameterBool` | `{src}_pitch`, `{src}_amp`, `{src}_egbias` |
| Master tune | 1 | `AudioParameterInt` 0–127 | |

That is 151 voice host parameters, 22 function host parameters and master tune: **174** in total.

- Function block: 6 modes (64–69) + 4 ranges + 12 bools = 22.
- Display names: `OP{n} {Parameter}` (e.g. `OP3 EG Rate 2`), `{Parameter}` for globals.
- Host order: OP1…OP6 blocks, then globals, then function, then master tune.
- Value strings match the LCD (R-P8): algorithm 1–32, detune −7…+7, transpose and break point as note names, wave names, ON/OFF, master tune as cents with one decimal.
- Name, banks, MIDI channel, omni, SYS INFO, memory protect, master volume and settings live only in state.
- **Host programs:** 32, one per internal slot, named after the voice. Selecting program *i* acts as MIDI program change *i*. The current program is the slot when the voice source is INT, otherwise 0.

---

## 5. Screens

### 5.1 Canvas and scaling

- The UI is one fixed layout of **1280 × 1000 logical px**, drawn entirely as SVG/HTML vectors. There are no raster images.
- The layout is scaled uniformly to fit the window, keeping the 1.28:1 aspect ratio and letterboxing the remainder in `--body`. The scale is clamped to 50–200 %.
- The layout never reflows.
- Zoom setting:
  - **Plugin and standalone:** the zoom sets the window size (zoom × 1280 × 1000). Resizing the window afterwards sets the scale directly, with a fixed-aspect constrainer, minimum 640 × 500 and maximum 2560 × 2000.
  - **Web:** the zoom caps the scale; the page fits the viewport up to that cap.
- First open: the plugin window opens at 100 % if that fits the screen, otherwise at the largest 10 % step that fits. The standalone window does the same. The web page fits the viewport.

### 5.2 Front panel (y 0–260)

| Element | Box (x, y, w, h) | Notes |
| --- | --- | --- |
| Pitch-bend wheel | 20, 70, 34, 150 | Springs back to centre on release, including a release outside the window. Follows incoming MIDI pitch bend when not being dragged. |
| Mod wheel | 64, 70, 34, 150 | Sends/receives the wheel controller (CC1). Follows incoming CC1 when not being dragged. |
| Wordmark | 120, 14, 420, 30 | `VDX` (Oswald 500, 28 px, letter-spacing 0.12em) + `6-OPERATOR FM SYNTHESIZER` (Barlow Condensed 600, 13 px) |
| Volume slider | 124, 70, 36, 150 | Master volume (§4.3). CC7 moves it. |
| Data-entry slider | 176, 70, 36, 150 | R-P3 |
| NO/−1, YES/+1 | 226, 96, 64, 40 and 226, 152, 64, 40 | Green buttons. Legends: `NO`/`OFF`/`−1`/`<` and `YES`/`ON`/`+1`/`>`. |
| LED | 316, 60, 72, 46 | 2-digit 7-segment, `--led-on` on `--led-off` |
| LCD | 316, 118, 232, 74 | Bezel; 16×2 cells, each 5×8 dots, dot pitch 2.4 px, cell pitch 14 × 24 px |
| Mode buttons | 566, 70, 132, 150 | 2 columns × 4 rows. Left column, top to bottom: MEMORY SELECT INTERNAL, MEMORY SELECT CARTRIDGE, EDIT/COMPARE, FUNCTION. Right column: STORE (`--salmon`), MEMORY PROTECT INTERNAL, MEMORY PROTECT CARTRIDGE, OPERATOR SELECT. All except STORE are `--dx-green`. |
| 32 buttons | 712, 60, 464, 180 | Two rows of 16: 1–16 on top at y 92, 17–32 below at y 182. Each is 26 × 36 at a 29 px pitch. The EDIT legend (`--violet`) sits above each button and the FUNCTION legend (`--brown-legend`) below it. The number sits inside, left, in `--legend`; the naming character sits inside, right, in `--body`. |
| Cartridge slot | 1192, 60, 72, 180 | Shows the cartridge name rotated 90° (reads bottom to top), ellipsis after 18 characters, or `NO CARTRIDGE` when absent. The full name is in a tooltip. Click opens the bank manager. |

### 5.3 LCD

The LCD shows two lines of 16 characters, columns 0–15.

**Format rule.** Every line below is literal text followed by its value field, starting at column 0, and padded with spaces to 16 characters. `NN` is a 2-character right-aligned number (" 7", "35").

**Cursor.** When a cursor is shown, its cell alternates every 400 ms between the character and a fully lit 5×8 block.

**Note names.** A 2-character name (`C `, `C#`, `D `, `D#`, `E `, `F `, `F#`, `G `, `G#`, `A `, `A#`, `B `) followed by the octave as a plain integer: octave = ⌊midi/12⌋ − 2, so MIDI 60 = `C 3` and MIDI 21 = `A -1`.

#### 5.3.1 PLAY

- **L1:** `INTERNAL VOICE` or `CARTRIDGE VOICE`, according to the selected memory.
- **L2:** the voice source `INT`/`CRT` (columns 0–2), the slot as `NN` (3–4), a space, then the edit buffer's name (6–15). Example: `INT 1 INIT VOICE`.
  - While EDITED, a cursor blinks at column 6.
  - While comparing, L2 shows the stored voice's name and no cursor.

#### 5.3.2 EDIT

**Line 1** (every EDIT page): `ALG` + `NN` (algorithm 1–32 at columns 3–4), a space, six digits at columns 6–11 (`1` = operator on; order OP1…OP6), a space, then `OPn` at columns 13–15 on the pages for buttons 16 and 18–28 and on button 17's oscillator-mode sub-page. Otherwise columns 13–15 are spaces. Example: `ALG 5 111111 OP4`.

**Line 2:**

| Btn | Line 2 format | Example |
| --- | --- | --- |
| 1–6 | unchanged from the page shown before the press | |
| 7 | `ALGORITHM SELECT` | |
| 8 | `FEEDBACK     =` NN | `FEEDBACK     = 7` |
| 9 | `LFO WAVE=` + `TRIANGL` / `SAW DWN` / `SAW UP` / `SQUARE` / `SINE` / `S/HOLD` | `LFO WAVE=TRIANGL` |
| 10 / 11 | `LFO SPEED    =` NN / `LFO DELAY    =` NN | `LFO SPEED    =35` |
| 12 / 13 | `LFO PM DEPTH =` NN / `LFO AM DEPTH =` NN | `LFO PM DEPTH = 0` |
| 14 | `LFO KEY SYNC=` + `ON`/`OFF` | `LFO KEY SYNC=ON` |
| 15 / 16 | `P MOD SENS.  =` NN / `A MOD SENS.  =` NN | `P MOD SENS.  = 3` |
| 17, sub-page mode | `FREQUENCY(RATIO)` or `FIXED FREQ.(Hz)` | |
| 17, sub-page sync | `OSC KEY SYNC=` + `ON`/`OFF` | `OSC KEY SYNC=ON` |
| 18 / 19 ratio | `F COARSE=` / `F FINE  =` + ratio as 5 characters `dd.dd` (leading space below 10) | `F COARSE= 1.00` |
| 18 / 19 fixed | `F COARSE=` / `F FINE  =` + frequency as 7 characters | `F COARSE=1.000Hz` |
| 20 | `OSC DETUNE   =` + `-7`…`-1`, ` 0`, `+1`…`+7` | `OSC DETUNE   = 0` |
| 21 / 22 | `EG  RATE  s  =` NN / `EG  LEVEL s  =` NN (s = stage 1–4, column 10) | `EG  RATE  1  =99` |
| 23 | `BREAK POINT=` + note name of MIDI (21 + value) | `BREAK POINT=A -1`, `BREAK POINT=C 3` |
| 24 | `L KEY SCALE=` / `R KEY SCALE=` + `-LIN` / `-EXP` / `+EXP` / `+LIN` | `R KEY SCALE=+EXP` |
| 25 | `L SCALE DEPTH=` NN / `R SCALE DEPTH=` NN | `R SCALE DEPTH=99` |
| 26 / 27 / 28 | `RATE SCALING =` NN / `OUTPUT LEVEL =` NN / `KEY VELOCITY =` NN | `OUTPUT LEVEL =99` |
| 29 / 30 | `P EG RATE  s =` NN / `P EG LEVEL s =` NN (s at column 11) | `P EG LEVEL 1 =50` |
| 31 | `MIDDLE C = ` + note name of MIDI (36 + transpose) | `MIDDLE C = C 3` |
| 32 | `NAME= ` + the 10-character name at columns 6–15 | `NAME= INIT VOICE` |

**Ratio value.** ratio × 100 = coarse × (100 + fine) for coarse ≥ 1, or 50 + ⌊fine/2⌋ for coarse 0. It is printed as `dd.dd`.

**Fixed value.** f = 1000 × 10^(fine/100), rounded to the nearest integer (4 digits, 1000–9772). The decimal point goes after digit (coarse & 3) + 1: `1.000Hz`, `10.00Hz`, `100.0Hz`. For coarse & 3 = 3 there is no point: `1000 Hz`, with a space before `Hz`.

**On the 32 page** (VOICE NAME) in EDIT_NAME, the cursor blinks on the character being edited.

#### 5.3.3 FUNCTION

L1 is `FUNCTION CONTROL`, except on pages 15/16 and in dialogs and messages.

| Btn | L2 format | Example |
| --- | --- | --- |
| 1 | `MASTER TUNE ADJ` (no value; data entry adjusts it) | |
| 2 | `POLY MODE` / `MONO MODE` | |
| 3 | `P BEND RANGE=` NN (shows 12 when step > 0) | `P BEND RANGE= 2` |
| 4 | `P BEND STEP=` NN | `P BEND STEP= 0` |
| 5 | poly: `SUS-KEY P RETAIN` / `SUS-KEY P FOLLOW`; mono: `FINGERED PORTA` / `FULL TIME PORTA` | |
| 6 | `GLISSANDO=` + `ON`/`OFF` | `GLISSANDO=OFF` |
| 7 | `PORTA TIME=` NN | `PORTA TIME= 0` |
| 8 sub 0 | `MIDI CH=` NN | `MIDI CH= 1` |
| 8 sub 1 | `SYS INFO AVAIL` / `SYS INFO UNAVAIL` | |
| 8 sub 2 (only while AVAIL) | ` MIDI TRANSMIT ?` | |
| 9 / 10 / 11 | `EDIT RECALL ?` / `VOICE INIT ?` / `CARTRIDGE FORM ?`; after the first YES: ` ARE YOU SURE ? ` | |
| 12 / 13 | the press has no effect at all | |
| 14 | `BATTERY VOLT=3.0` | |
| 15 / 16 | L1 ` SAVE MEMORY` / ` LOAD MEMORY`; L2 ` ALL OF MEMORY ?`; after the first YES: ` ARE YOU SURE ? ` | |
| 17–32 | SRC + ` ` + PARAM + `=` + value. SRC = `WHEEL `, `FOOT  `, `BREATH`, `AFTER `; PARAM = `RANGE`, `PITCH`, `AMP  `, `EG B.`; value = NN for RANGE, `ON`/`OFF` otherwise | `WHEEL  RANGE=99`, `FOOT   AMP  =OFF`, `BREATH EG B.=ON` |

#### 5.3.4 Dialogs (shown until answered or cancelled, F-2)

| Dialog | L1 | L2 |
| --- | --- | --- |
| Memory protect | ` MEMORY PROTECT` | ` INTERNAL` or ` CARTRIDGE`, padded, with `ON`/`OFF` from column 13 (` INTERNAL    ON `) |
| Store (PLAY) | the PLAY L1 | ` MEMORY STORE` |
| EG copy (EDIT) | ` EG COPY` | `from OPs to OP?` (s = selected operator) |
| Confirm (FUNCTION 9/10/11/15/16) | as the page | ` ARE YOU SURE ? ` |

#### 5.3.5 Messages (shown for 2 s)

| Message | L1 | L2 |
| --- | --- | --- |
| Memory protected | unchanged | `MEMORY PROTECTED` |
| Not a valid file or bank | unchanged | `FORMAT CONFLICT!` |
| Checksum mismatch | unchanged | `CHECK SUM ERROR!` |
| SysEx accepted (dumps only, not parameter changes) | unchanged | ` MIDI RECEIVED` |
| Operation completed | unchanged | ` COMPLETED` |
| State reset (§4.8) | PLAY L1 | `  STATE RESET   ` |
| No cartridge | `   NOT READY !  ` | `INSERT CARTRIDGE` |
| Power-on splash (1.5 s, not 2 s) | `      VDX       ` | ` FM SYNTHESIZER ` |

### 5.4 LED and lamps

- **LED:** the slot 1–32 of the voice source, with no leading zero (the tens digit is blank for 1–9). The decimal point after the last digit is lit while EDITED and not comparing. The LED is blank during the splash.
- **Lamps**, lit by the UI from core state:
  - **PLAY:** the selected-memory button is lit. The numbered button of the voice source's slot is lit while the selected memory equals the voice source's bank.
  - **EDIT / EDIT_NAME:** EDIT/COMPARE is lit, and the numbered button of the current page. During COMPARE, the EDIT/COMPARE lamp blinks (400 ms on, 400 ms off).
  - **FUNCTION:** FUNCTION is lit, and the numbered button of the current page.
  - **Dialogs:** STORE is lit while a store or EG-copy dialog is pending. MEMORY PROTECT INTERNAL/CARTRIDGE is lit while its dialog is shown.

### 5.5 Programmer (y 270–1000)

| Block | Box | Contents |
| --- | --- | --- |
| Algorithm chart | 16, 270, 400, 250 | All 32 algorithms as an 8 × 4 grid of 48 × 58 thumbnails in reading order (1–8 on the first row). Each thumbnail's background tint marks its group: 1–6, 7–18, 19–25, 26–32 (four tints). The current algorithm is outlined. Click selects. |
| Algorithm detail | 428, 270, 200, 250 | Large diagram of the current algorithm: carriers on the bottom row, modulators stacked above, feedback loop drawn; numeric field 1–32 |
| Voice globals | 640, 270, 220, 250 | Feedback, osc key sync, transpose (note name, as the LCD), name text field, pitch mod sens |
| LFO | 872, 270, 196, 250 | Wave (6 icon buttons), speed (number plus Hz with 2 decimals, from the LFO frequency table), delay, PMD, AMD, key sync |
| Pitch EG | 1080, 270, 184, 250 | Graph with 4 draggable points, plus 8 numeric fields |
| Operators | 16 + i·208, 532, 200, 368 (i = 0…5) | One panel per operator (§5.6), ordered per the Operator order setting |
| Function strip | 16, 912, 1248, 76 | Poly/mono, bend range/step, porta mode/gliss/time, master tune (cents), master volume, then 4 controller columns (range + PITCH/AMP/EG B toggles) |

Value formats in the programmer match the LCD: algorithm 1–32, detune −7…+7, break point and transpose as note names, ratio `dd.dd`, fixed frequency as `1.000Hz` and so on, wave names, ON/OFF.

### 5.6 Operator panel (200 × 368)

From top to bottom:
1. Header: `OP n`, on/off switch, and a carrier/modulator badge (`--carrier` / `--modulator`, text in `--body`) for the current algorithm.
2. Frequency row: ratio/fixed toggle, coarse, fine, detune, and the computed ratio or frequency as on the LCD.
3. Output level (large).
4. Velocity sensitivity, AMS, rate scaling.
5. EG graph (184 × 90) with four draggable rate/level handles, plus 8 numeric fields.
6. Level-scaling graph (184 × 60) with a draggable break point, L/R curve pickers and L/R depths.
7. Output meter: −60…0 dBFS, peak with a 300 ms fall, from meter events.

### 5.7 Bank manager (overlay, 960 × 640, centred)

**Layout:**
- **Left and right columns:** the internal bank and the cartridge, each with 32 rows showing the slot number and voice name.
- **Header of each column:**
  - the bank name (`INTERNAL`, or the cartridge name with a `•` when changed);
  - a memory-protect toggle mirroring the panel global;
  - Export… (writes a 4104-byte `.syx`).
- **Below the columns:** an "edit buffer" drop target showing the edit buffer's name.
- **Buttons:**
  - Load file…
  - New cartridge
  - Eject cartridge
  - Export voice… (the edit buffer as a 163-byte `.syx`)
  - Send bank (MIDI)
  - Request dump (MIDI)
  - Close

**States:**

| State | What the user sees |
| --- | --- |
| No cartridge | The right column shows `NO CARTRIDGE` and a "Load file…" button. Eject is disabled. |
| Protected target | A drop or load onto a protected bank does nothing. An inline notice reads "Internal memory is protected." or "Cartridge memory is protected." for 4 s, and the LCD shows MEMORY PROTECTED. |
| Rejected file | An inline notice reads "Not a DX7 voice or bank file: ‹file name›" for 4 s, and the LCD shows FORMAT CONFLICT!. |
| MIDI unavailable | Send bank and Request dump are disabled with a tooltip ("MIDI output not available" or "SysEx not permitted"). |
| Waiting for dump | A "Waiting for bank…" label for up to 10 s, then "No reply." |

**Interaction:**
- Dragging a row onto a row copies that voice over the target, without confirmation.
- Dragging a row onto the edit-buffer target loads it into the edit buffer (as a voice selection, R-P5).
- Double-clicking a row selects that voice (as pressing its button in PLAY).
- Esc, the Close button, or a click outside the overlay closes it.

### 5.8 Settings (overlay, 960 × 640)

**Contents:**
- the §4.4 settings;
- MIDI receive channel and omni (the same values as FUNCTION 8);
- standalone: an "Audio/MIDI devices…" button that opens JUCE's standard device selector (output device, sample rate, buffer size, MIDI inputs);
- web: checkboxes for the Web MIDI inputs and a single choice of MIDI output.

**States:**
- No Web MIDI (unsupported, or permission denied): the MIDI part reads "MIDI not available — use the computer keyboard."
- MIDI granted without SysEx: the line "SysEx not permitted — dumps disabled."

Unplugged devices disappear from the lists without an error. Settings changes apply immediately.

### 5.9 Web page states

| State | What is shown |
| --- | --- |
| Before the first user gesture | The full UI is shown with the LCD unlit (all cells `--lcd-off`) and the LED blank. A centred card reads "Click or press any key to start VDX". The click or key that dismisses the card is not passed on to any control or to the keyboard. Audio then starts, the splash runs, then PLAY. |
| Not cross-origin isolated, no AudioWorklet, or WASM failed to load | The UI is replaced by a centred message naming the problem and how to fix it, for example: "VDX needs to be opened from http://localhost:5173 (cross-origin isolation is off)." |
| No audio output possible (the context fails to start) | "No audio output available." Everything else stays usable. |

### 5.10 Plugin and standalone fallback

If the system web view (WebView2 / WebKitGTK) is missing, the editor shows a plain JUCE label: "VDX needs Microsoft Edge WebView2 / WebKitGTK to show its panel." The engine, MIDI, automation and state keep working.

---

## 6. Flows

### F-1 Start

1. **Standalone:**
   1. Load state from the state file (§4.8, R-D5).
   2. Open the window (§5.1).
   3. Show the splash for 1.5 s, then PLAY.
2. **Plugin:**
   1. When the host creates the instance: first-run state, or the host's saved state.
   2. The splash runs once per instance, the first time its editor opens. Later opens show the current panel state.
3. **Web:**
   1. Load the page and its state from IndexedDB.
   2. Show the start card (§5.9).
   3. On the first click or key: start audio, request Web MIDI with `{ sysex: true }`, show the splash, then PLAY.
4. **During the splash:** front-panel inputs are ignored; MIDI, notes and the computer keyboard work.
5. **After the splash:** PLAY, selected memory = the voice source's bank. Last edit page = 7, last function page = 1, selected operator = 1, EG stage = 1, P EG stage = 1, scale side = L, button-17 sub-page = mode, button-8 sub-page = 0.

### F-2 Front-panel transitions

**General rules:**
- **Dialog pending:** the dialog's own answers act on it. Any other front-panel input cancels the dialog, then acts as below. The data-entry slider is ignored while a dialog is pending.
- **Message showing:** any front-panel input dismisses it, then acts as below. A new message replaces the current one and restarts its 2 s.
- **YES/NO held:** auto-repeats after 500 ms, at 10 steps/s.

**Mode buttons:**

| Input | PLAY | EDIT | EDIT_NAME | FUNCTION |
| --- | --- | --- | --- | --- |
| MEMORY SELECT INT | selected memory = INT | → PLAY, selected memory = INT (ends compare) | types `-` | → PLAY, selected memory = INT |
| MEMORY SELECT CRT | selected memory = CRT. With no cartridge: NOT READY message, nothing else changes. | → PLAY with CRT; with no cartridge: NOT READY, nothing else changes | types `.` | as EDIT |
| EDIT/COMPARE | → EDIT on the last edit page | CLEAN: no effect. EDITED: toggles COMPARE (F-9). | types a space | → EDIT on the last edit page |
| FUNCTION | → FUNCTION on the last function page | → FUNCTION (ends compare) | → EDIT, VOICE NAME page, no cursor | no effect |
| STORE | store dialog (F-7) | EG-copy dialog (F-8); no effect while comparing | types `W` | no effect |
| MEMORY PROTECT INT / CRT | protect dialog (F-11) | protect dialog | types `X` / `Y` | protect dialog |
| OPERATOR SELECT | no effect | selected operator = the next enabled operator after it (1→6, wrapping). No effect when no operator is enabled. | types `Z` | no effect |

**Numbered buttons and data entry:**

| Input | PLAY | EDIT | EDIT_NAME | FUNCTION |
| --- | --- | --- | --- | --- |
| Buttons 1–32 | select that slot of the selected memory (F-6) | 1–6: toggle that operator's mask bit (the page and line 2 stay). 7–31: show that page; pressing the current page's button again cycles its sub-page (17: mode/sync; 21 and 22: stage 1→4; 29 and 30: stage 1→4; 24 and 25: L/R). 32: → EDIT_NAME (F-4). No effect while comparing. | type the character (F-4) | show that page; pressing 8 again cycles 0→1→2→0 (2 is skipped while UNAVAIL); 12/13 no effect; 9–11, 15, 16 start the page's question (F-10) |
| NO / YES | no effect | −1 / +1 on the page's value (on/off pages: NO = off, YES = on; text choices step through the list). Page 7 steps the algorithm. Page 32 outside naming: no effect. No effect while comparing. | cursor left / right (clamped to characters 1–10) | −1 / +1 on the page's value. Page 8 sub 1: YES = AVAIL, NO = UNAVAIL. Page 8 sub 2 and pages 9–11, 15, 16: answers (F-10). Page 14: no effect. |
| Data entry | ignored | sets the page's value to round(pos × max) (on/off: pos ≥ 0.5 = on; text choices: round(pos × (count − 1))). Ignored while comparing. | ignored | same as EDIT for value pages; ignored on pages 8 sub 1–2, 9–16 |

**Shared sub-page indices:**
- 21 and 22 share one EG stage index.
- 29 and 30 share one P EG stage index.
- 24 and 25 share one L/R side.

Returning to a page keeps its sub-page.

**Data-entry slider thumb.** It always shows the current page's value: it jumps to value/max on every page change and on every external change to that value.

### F-3 Editing from the programmer

1. **Change a value:** drag, wheel, typed entry or a toggle (R-U1).
2. **What follows from the change:**
   - It cancels any pending dialog or naming.
   - If the panel is in PLAY or FUNCTION and a voice parameter changed, the panel switches to EDIT and shows that parameter's page.
   - If a function parameter changed and the panel is not in FUNCTION, the panel switches to FUNCTION and shows that page.
   - For operator parameters, the selected operator becomes that operator, even if it is disabled.
   - The edit buffer becomes EDITED (voice parameters except the operator mask).
3. **While comparing:** a programmer change first ends COMPARE (the edit buffer returns), then applies.
4. **"Touching"** means changing a value. Focus or hover alone changes nothing.

### F-4 Naming a voice

1. In EDIT, press 32. The panel enters EDIT_NAME with the cursor on character 1 (LCD column 6).
2. **Typing.** Each front-panel input types one character at the cursor, then the cursor moves right. At character 10 the cursor stays, and further typing overwrites character 10.
   - Buttons 1–32 → `1 2 3 4 5 6 7 8 9 0 A B C D E F G H I J K L M N O P Q R S T U V`
   - STORE → `W`
   - MEM PROTECT INT → `X`
   - MEM PROTECT CRT → `Y`
   - OPERATOR SELECT → `Z`
   - EDIT → space
   - MEM SELECT INT → `-`
   - MEM SELECT CRT → `.`
3. YES and NO move the cursor right and left.
4. **Leaving naming:**
   - FUNCTION → EDIT, VOICE NAME page, no cursor.
   - Any programmer change (F-3) also ends naming.
   - Selecting a voice by program change or bank manager ends naming and returns to the page that applies.
5. **Programmer name field:**
   - It accepts 0x20–0x7E. Other characters (including non-ASCII, emoji and RTL) are replaced with `?`.
   - Pasted text is truncated to 10 characters; shorter names are padded with spaces.
   - Physical keys typed into this field never play notes.
   - Physical keys never type into the LCD name.

### F-5 Key transpose

1. In EDIT, pressing 31 shows the KEY TRANSPOSE page and **arms** it. Pressing 31 again re-arms it.
2. While armed, the next note-on from any source (MIDI, computer keyboard, host) does three things:
   - sets transpose = clamp(note − 36, 0, 48);
   - does **not** sound;
   - disarms the page.

   The LCD now names the pressed key: pressing C2 (MIDI 48) gives `MIDDLE C = C 2`, and everything plays 12 semitones lower.
3. Leaving the page disarms it.

### F-6 Selecting a voice

1. A voice is selected in one of four ways:
   - a numbered button in PLAY;
   - a program change;
   - a host program;
   - a double-click or drop in the bank manager.
2. If the edit buffer is EDITED, copy it to the recall buffer first.
3. Load the slot into the edit buffer and the stored voice. Then:
   - set the voice source;
   - CLEAN;
   - reset the operator mask to all-on;
   - end COMPARE.
4. Notes keep sounding and switch to the new voice data immediately (Mk I behaviour).
5. The LCD, LED and lamps update. The panel mode is unchanged, except that naming ends.

### F-7 Store (PLAY)

1. STORE → store dialog. STORE again, NO, or another input cancels it.
2. Press 1–32 → target = that slot of the selected memory at the time of this press.
3. **Target protected:** MEMORY PROTECTED message; nothing is written; the dialog ends.
4. **Otherwise:**
   - write the edit buffer to the target, set the stored voice to the edit buffer, and set the voice source to the target;
   - CLEAN;
   - the LCD returns to the PLAY page with the LED on the new slot;
   - if the target is the cartridge, set the cartridge's changed flag.

### F-8 EG copy (EDIT)

1. STORE → EG-copy dialog showing `from OPs to OP?`.
2. Press 1–6:
   - **Different operator:** copy the selected operator's R1–R4 and L1–L4 to it; the edit buffer becomes EDITED.
   - **Same operator:** nothing happens.

   Either way, the dialog ends and the previous page returns.
3. NO, STORE, or any other input cancels the dialog.

### F-9 Compare

1. In EDIT with EDITED, press EDIT/COMPARE:
   - the stored voice sounds;
   - the LCD shows the PLAY page with the stored voice's name;
   - the LED decimal point goes off;
   - the EDIT lamp blinks.
2. Numbered buttons, YES/NO and data entry do nothing while comparing.
3. EDIT/COMPARE again → the edit buffer sounds again and the last edit page returns.
4. MEMORY SELECT, FUNCTION, programmer edits, host automation, a received SysEx parameter change, and a voice selection all **end** COMPARE first, then act.
5. Host parameters always reflect the edit buffer, never the stored voice.

### F-10 FUNCTION confirmations

The confirmations are on pages 9 EDIT RECALL, 10 VOICE INIT, 11 CARTRIDGE FORMAT, 15 CARTRIDGE SAVE and 16 CARTRIDGE LOAD.

1. On the question, YES → ` ARE YOU SURE ? `. YES again → execute.
2. NO at either step → back to the question text.
3. Any other input cancels and acts.
4. **Execution:**
   - **9:** recall buffer → edit buffer; EDITED; operator mask all-on.
   - **10:** INIT VOICE (R-P10) → edit buffer; EDITED; operator mask all-on.
   - **11:** needs a cartridge and cartridge protect off. Fills the cartridge with 32 × INIT VOICE and sets its changed flag. The cartridge name is kept.
   - **15 (save):** needs a cartridge and cartridge protect off. Copies internal → cartridge, all 32 slots, and sets the changed flag.
   - **16 (load):** needs a cartridge and internal protect off. Copies cartridge → internal.
5. **Results:**
   - Success shows ` COMPLETED`, then the question text again.
   - No cartridge: NOT READY.
   - Protected: MEMORY PROTECTED.
6. **Page 8 sub 2:** YES sends the internal bank (4104 bytes) to MIDI out and shows ` COMPLETED`. NO does nothing.

### F-11 Memory protect dialog

1. MEMORY PROTECT INT or CRT → dialog showing the current state.
2. YES → on, NO → off; the dialog stays.
3. Any other input ends the dialog, the previous page returns, and that input acts.

### F-12 Bank manager

1. **Load file…** opens the OS file dialog (web: the browser's file picker), filtered to `.syx`. Choosing a file:
   - **Voice (163 bytes):** goes to the edit buffer as a voice load. Recall capture as F-6. EDITED. The voice source is unchanged.
   - **Bank (4104 bytes, or a headerless 4096-byte bank):** a choice appears: "Insert as cartridge" (default) or "Load into internal".
     - Inserting replaces the cartridge (no protect check; it is like swapping cartridges) and clears the changed flag.
     - Loading into internal respects internal protect.
   - **Anything else:** rejected (§5.7).
   - **Cancel:** nothing changes.
2. **Dropping a `.syx` file** anywhere on the window does the same as Load file… with that file. In the bank choice, "Insert as cartridge" is preselected; Enter confirms.
3. **New cartridge:** inserts `UNTITLED` with 32 × INIT VOICE. If a cartridge with the changed flag set is present, a confirmation appears first: "Replace the current cartridge? Unsaved changes will be lost." with Replace / Cancel.
4. **Eject cartridge:** the same confirmation when changed, then the cartridge is removed. If the selected memory was CRT, it becomes INT. The voice source keeps showing CRT.
5. **Export:**
   - Writes a file through the OS save dialog (web: a download), named `<bank or voice name>.syx` with characters outside `A–Z a–z 0–9 - _ space` replaced by `_`.
   - Exporting the cartridge clears its changed flag.
   - Standalone only: a failed write shows the inline notice "Could not write ‹path›".
6. **Request dump:**
   1. Sends `F0 43 20 09 F7`.
   2. A bank arriving within 10 s is handled as F-13.
   3. Otherwise "No reply." is shown.
7. **Send bank:** sends the internal bank, as FUNCTION 8 TRANSMIT.

### F-13 SysEx over MIDI

1. **Filter:** received SysEx is accepted only when SYS INFO AVAIL is on, the manufacturer is 0x43, and the device nibble `n` equals the receive channel − 1 (any `n` while omni is on). Otherwise it is ignored silently.
2. **Single voice:** goes to the edit buffer (recall capture as F-6), EDITED, operator mask all-on. ` MIDI RECEIVED` is shown, then the current page re-renders.
3. **Bank:** goes to the internal bank if internal protect is off, and ` MIDI RECEIVED` is shown. Otherwise MEMORY PROTECTED is shown and the bank is discarded. The edit buffer is unchanged.
4. **Parameter change:** applied like host automation (R-S2). No message is shown.
5. **Dump request:** answered on transmit channel 1 with the edit buffer (`00`) or the internal bank (`09`), only while AVAIL.
6. **Transmit:** while AVAIL, every front-panel or programmer edit sends one parameter-change message. A drag sends at most one message per parameter every 10 ms, and always the final value. Changes caused by automation or by received MIDI are never transmitted.

### F-14 Program change

1. Program 0–31 selects internal slot 1–32.
2. Program 32–63 selects cartridge slot 1–32 when a cartridge is present; otherwise it is ignored.
3. Programs 64–127 are ignored.
4. A program change selects a voice as in F-6, sets the selected memory to the slot's bank, and cancels any pending dialog.

### F-15 Close, reload, relaunch

1. **Standalone:**
   - Changes are saved 1 s after the last change, at least every 5 s during continuous changes, and once more on quit.
   - Saving writes a temporary file, then renames it over `state.bin`.
   - `internal.syx` is written at the same time.
   - If the directory can't be created or written, the status line of the Settings overlay shows "Could not save state: ‹reason›", and VDX keeps running.
2. **Plugin:** state is saved whenever the host asks.
   - Closing the editor releases computer-keyboard notes, returns the pitch wheel to centre, and leaves the engine running.
3. **Web:**
   - State is saved with the same timing as the standalone, plus a final save on `visibilitychange` (hidden) and `pagehide`.
   - Without IndexedDB, VDX keeps running in memory only and shows a one-line banner: "Changes will not be saved in this browser."
4. **Interrupted actions.** An action interrupted by a close or reload is lost, along with any edits after the last save. Examples: a dialog, a transmit in progress, or a half-received dump. The next start shows the last saved state.

---

## 7. Rules

The sound-engine rules are product behaviour: they define what VDX sounds like, and the Tier 1 tests in §13 check them numerically. Units, Q formats and formulas below are binding where an acceptance criterion depends on them.

### 7.1 Sound engine

**R-E1 Rate, blocks, output path.**
- The internal rate is **49,096 Hz**. The block size is N = 64 samples.
- LFO, pitch EG, portamento and controller smoothing update once per block. Envelopes update per sample (R-E4).
- **Resampling.** Output is resampled to the host rate with r8brain-free-src (24-bit preset), mono, then copied to both channels. A mono output bus is accepted and gets the same signal.
- **Latency.** `latencySamples()` reports the resampler delay plus internal block buffering at the host rate. The plugin reports it to the host with `setLatencySamples`.
- **Render at host rate.** With this setting on, there is no resampler. Per-sample EG tick periods and hold counts are scaled by hostRate / 49,096 using a fractional accumulator. The DAC filter coefficients are computed for the host rate. LFO and pitch-EG tables scale as in MSFA.
- **Web.**
  - Request `new AudioContext({ sampleRate: 49096 })`. If that throws, or the context reports another rate, use a default-rate context plus the resampler.
  - Audio is rendered in an AudioWorklet that runs the same `vdx_core` compiled to WASM.

**R-E2 Units.**

| Quantity | Unit |
| --- | --- |
| Envelope level `L` | 12-bit counts, 1 count = 1/256 octave ≈ 0.0235 dB. Valid range 16…4095. For L1 = OL = 99 at velocity 100 the level is **3840**. The floor is 16. |
| Operator attenuation | Q10 log2 (1 unit = 1/1024 octave) |
| Phase | 32-bit accumulator, one cycle = 2^24 (MSFA Q24 convention) |
| Log frequency | MSFA Q24 log2 Hz; Mk I mode quantises it to multiples of 2^14 (R-E6) |
| Operator output | Q24 signal. L = 3584 gives ±2^24, so L = 3840 gives ±2^25 and L = 4095 gives just under ±2^26. |

**R-E3 Mk I operator kernel.** The tables are generated at startup or committed as generated integer tables (§14):
- `logsin[i] = round(−1024 · log2(sin((i + 0.5) / 1024 · π/2)))`, i = 0…1023.
- `exptab[i] = round(2^(13 − i/1024))`, i = 0…1023 (values 8192 … 4099).

Per sample, for one operator:
```
p     = (phase + modInput) & 0xFFFFFF            // modInput: Q24 signal added to phase (MSFA convention)
q     = p >> 22;  i = (p >> 12) & 1023;  if (q & 1) i = 1023 − i;
att   = logsin[i] + ((4096 − min(L, 4095)) << 2) + carrierAtt     // all Q10
y14   = (att >> 10) >= 14 ? 0 : exptab[att & 1023] >> (att >> 10)
y     = (q >= 2 ? −y14 : y14) << 13              // Q24, matches MSFA's fm_op_kernel scale
```
- `carrierAtt` = `round(1024 · log2(C))` for carriers, where C is the algorithm's carrier count (R-E8); 0 for modulators.
- A masked-off operator outputs 0, and its envelope keeps running.

**R-E4 Envelope generator (per sample).** This ports MSFA/Dexed `env.cc` semantics and changes only the timing.
- `qrate = (rate × 41) >> 6` (0…63), then rate scaling: `qrate = min(63, qrate + ((sens × clamp(note/3 − 7, 0, 31)) >> 3))`.
- **Target level:** `actual = ((scaleoutlevel(levelParam) >> 1) << 6) + outlevel − 4256`, clamped to 16…4095.
  - `outlevel` is defined in R-E5.
  - `scaleoutlevel(x) = x ≥ 20 ? 28 + x : LUT[x]`, with LUT = [0,5,9,13,17,20,23,25,27,29,31,33,35,37,39,41,42,43,45,46].
- **Clock.** There is one state update per internal sample; tick counter `c` increments each sample.
  - Gate mask by `qrate & 3`: 0 → `01010101`, 1 → `01010111`, 2 → `01110111`, 3 → `01111111`. The bit index is `tick & 7`, MSB first.
  - `qrate < 48`: a tick every `T = 2^(11 − (qrate >> 2))` samples, `step = 1`.
  - `qrate ≥ 48`: a tick every sample, `step = 1 << ((qrate >> 2) − 11)`.
  - On a tick whose mask bit is 1:
    - decay: `L −= step`, not below the target;
    - attack: `L += step × (17 − (L >> 8))`, not above the target.
- **Attack start:** if `L < 1716` when an attack segment begins, set `L = 1716`.
- **Holds:** a segment whose target equals the current level holds for the time in Dexed's `statics[]` (rates 0–76; `20 × (99 − rate)` above 76). The counts are rescaled from 44.1 kHz to 49,096 Hz (`× 49096/44100`, rounded).
- **Key-off** jumps to segment 4 (R4 → L4) from the current level.

**R-E5 Operator level, velocity, scaling.**
```
outlevel  = scaleoutlevel(OL) + ScaleLevel(note, BP, LD, RD, LC, RC)   // MSFA dx7note.cc
outlevel  = min(127, outlevel)
outlevel  = (outlevel << 5) + ScaleVelocity(velocity, KVS)              // shift first, then add
```
- **ScaleLevel:**
  - The breakpoint offset is `note − BP − 17`, grouped by 3 semitones.
  - Linear curves: `(group × depth × 329) >> 12`.
  - Exponential curves use MSFA's 33-entry `exp_scale_data`.
  - Curve codes: 0 −LIN, 1 −EXP, 2 +EXP, 3 +LIN.
- **ScaleVelocity:** `((KVS × (velocity_data[v >> 1] − 239) + 7) >> 3) << 4`, with MSFA's 64-entry table. Velocity 100 is the 0 dB reference.
- **Classic velocity scaling** (setting): incoming velocity × 100/127, rounded, before use.

**R-E6 Frequency.**
- **Note.** The note used is incoming note + (transpose − 24), folded by octaves into 0–127.
- **Ratio mode.** Q24 logfreq = note logfreq + `coarsemul[coarse]` (coarse 0 = ×0.5, 1–31 = ×coarse) + `round(2^24/ln 2 × ln(1 + 0.01 × fine))` + detune.
- **Detune** (Dexed `dx7note.cc`, Apache): `detuneRatio = 0.0209 × exp(−0.396 × logfreq / 2^24) / 7`; add `detuneRatio × logfreq × (detune − 7)`. In fixed mode, add `13457 × (detune − 7)` only when `detune > 7`.
- **Fixed mode:** `10^(coarse & 3) × 10^(fine/100)` Hz.
- **Mk I mode:** after all additions (pitch bend, LFO, pitch EG, master tune), round logfreq to a multiple of 2^14 before converting to a phase increment.
- **Master tune:** cents = (v − 64) × 75/64, applied as a logfreq offset.
- **Pitch bend:** range from function 65. When step (function 66) > 0, the bend is quantised to multiples of `step` semitones (round to nearest) over a 12-semitone range.

**R-E7 LFO, pitch EG, sensitivities.**
- **LFO:** MSFA `lfo.cc` as is.
  - Speed table: 0.0625 Hz at 0 … 49.3 Hz at 99.
  - Waves: TRI, SAW DN, SAW UP, SQU, SIN, S/H. The S/H LCG is `(x × 179 + 17) & 0xFF`.
  - Key sync resets the phase to 2^31 − 1.
  - Two-stage delay.
  - Retrigger only when no other key is held.
- **Pitch mod sensitivity** 0–7 → {0, 10, 20, 33, 55, 92, 153, 255}/255 of MSFA's full pitch-mod range.
- **AM sensitivity** 0–3 → {0, 0.259, 0.427, 1.0} of MSFA's full AM depth.
- **Pitch EG:** MSFA `pitchenv.cc`. Level 50 = 0; the table is in 1/32-octave units; there is a 100-entry rate table.

**R-E8 Algorithms, feedback, carriers.**
- Routing follows the MSFA `FmAlgorithm` table. Operators are computed OP6 → OP1.
- Carrier counts for algorithms 1–32: {2,2,2,2,3,3,2,2,2,2,2,2,2,2,2,1,1,1,3,3,4,4,4,5,5,3,3,3,4,4,5,6}.
- **Feedback:** `fb = (y[n−1] + y[n−2]) >> (fbShift + 1)`, `fbShift = 8 − FB`; FB 0 = none.
- Algorithms 4 and 6 feed back across a chain (OP6←OP4 and OP6←OP5), as MSFA implements.

**R-E9 DAC stage** (Mk I mode, DAC emulation on). Per voice, after carrier summation:
```
s   = clamp(voiceSumQ24 >> 11, −16384, 16383)          // 15-bit signed
m   = |s|;  e = m < 2048 ? 0 : m < 4096 ? 1 : m < 8192 ? 2 : 3
q   = sign(s) × ((m >> e) << e)                          // sign + 11-bit mantissa + 2-bit exponent
```
- Sum the quantised voices (the **DAC tap**, an int32).
- Then apply a 2-pole Butterworth low-pass at 16 kHz (TPT/SVF form at 49,096 Hz).
- Output: `float = sum / 2^16`. One voice at L = 3840 gives ≈ −12 dBFS.
- Without the DAC stage, use `voiceSumQ24 / 2^27`, which gives the same loudness, without the 15-bit clamp.

**R-E10 Output stage.** Multiply by the master volume gain (§4.3), then hard-clip to ±1.0. This applies in every build.

**R-E11 Voices.**
- **16 voices.** Allocation scores each voice: +4 idle, +2 released (key up, including voices held only by sustain), +1 same note. The highest score wins. Ties go to the voice with the earliest note-on.
- **A note-on for a note that is already sounding** starts a new voice. A note-off releases the oldest sounding instance of that note.
- **Sustain** (CC64 ≥ 64) holds released voices until sustain goes below 64.

**R-E12 Mono and portamento.**
- **Mono mode:**
  - Last-note priority over a note stack. Releasing the sounding note returns to the most recent still-held note, gliding if portamento applies.
  - **FINGERED:** glide only when legato (another key held). Legato notes do not retrigger EGs or the LFO.
  - **FULL TIME:** always glide. EGs retrigger only on non-legato notes.
- **Poly mode:**
  - **RETAIN:** voices held by sustain keep their pitch, and new notes glide from the last played note.
  - **FOLLOW:** voices held by sustain also glide to each new note.
- **Portamento** (Dexed `porta.cpp`, Apache):
  - Rate `2100 × 2^(−0.062 × time)` semitones/s; time 0 = off.
  - Glissando quantises the glide to semitones.
  - Portamento is active only while the portamento switch (CC65) is on. CC65 defaults to on and is not saved.

**R-E13 Controllers.**
- Wheel (CC1), foot (CC4), breath (CC2) and aftertouch (channel pressure) are combined with their range 0–99 and assign bits (pitch, amp, EG bias) as MSFA `controllers.h` does.
- All four start at 0 and are not saved.

**R-E14 Engine modes.**
- **Mk I** (default): R-E3 kernel, R-E6 frequency quantisation, DAC stage available.
- **Modern:** MSFA `fm_op_kernel` (24-bit), with the same envelope, voice and controller code, no frequency quantisation and no DAC stage. Carrier 1/C scaling is applied as a linear multiply, so both modes are equally loud.
- Switching engine mode or DAC takes effect at the next block, without stopping notes.
- Switching "render at host rate" stops all voices and re-prepares; notes resume on the next note-on.

**R-E15 Real-time safety.**
- No allocation, locks, logging or exceptions inside `render()` or `midi()`.
- Every recursive float path (filter and resampler state) adds or subtracts a 1e-20 DC offset: +1e-20 on even blocks and −1e-20 on odd blocks, added to the state update. This keeps values out of the subnormal range without flush-to-zero.
- **Block sizes.** Host block sizes from 1 to 8192 are accepted:
  - `n` = 0 is a no-op.
  - Blocks larger than the prepared maximum are processed in chunks.
  - MIDI events are applied at the start of the internal 64-sample block containing their sample offset.

### 7.2 MIDI input

**R-M1 Receive.**
- **Channel filter.** Channel messages are accepted on the receive channel, or on any channel while omni is on. This applies in every build, including the plugin.

| Message | Behaviour |
| --- | --- |
| Note on/off | Velocity 0 = off; the full 0–127 range is used |
| Channel aftertouch | Aftertouch controller |
| Pitch bend | 14-bit; range and step from function parameters |
| CC1 / CC2 / CC4 | Wheel / breath / foot controllers |
| CC5 | Sets function 69 to round(v × 99/127), as an external change (R-S2) |
| CC65 | Portamento switch (≥ 64 on) |
| CC64 | Sustain (≥ 64 on) |
| CC6 | Data entry: acts as the slider at position v/127 |
| CC96 / CC97 | YES/+1 / NO/−1 |
| CC7 | Sets master volume and moves the volume slider |
| CC120 / CC123 | All sound off / all notes off |
| Program change | F-14 |
| SysEx | F-13 |

**R-M2 MIDI stream robustness.**
- SysEx is reassembled across `midi()` calls up to 4104 bytes. Longer SysEx is discarded silently.
- Realtime bytes (0xF8–0xFF) inside SysEx are acted on (or ignored) and are not stored in the message.
- Running status is supported.

**R-M3 Transmit.** Per F-13, on MIDI channel 1. The standalone and web builds use the chosen MIDI output. The plugin uses the host's MIDI output (best effort: hosts that drop instrument MIDI output simply get nothing).

### 7.3 Front panel

**R-P1 Button map.**

| Btn | EDIT (violet legend) | Voice param | FUNCTION (brown legend) | Function param |
| --- | --- | --- | --- | --- |
| 1–6 | OPERATOR ON/OFF 1–6 (also the EG COPY target) | 155 bits 5…0 | 1 MASTER TUNE ADJ · 2 POLY/MONO · 3 P BEND RANGE · 4 P BEND STEP · 5 PORTAMENTO MODE · 6 GLISSANDO | master tune, 64, 65, 66, 67, 68 |
| 7 | ALGORITHM SELECT | 134 | PORTAMENTO TIME | 69 |
| 8 | FEEDBACK | 135 | MIDI (CH → SYS INFO → TRANSMIT) | panel globals |
| 9 | LFO WAVE | 142 | EDIT RECALL | — |
| 10 | LFO SPEED | 137 | VOICE INIT | — |
| 11 | LFO DELAY | 138 | CARTRIDGE FORMAT | — |
| 12 | LFO PM DEPTH | 139 | (no function) | — |
| 13 | LFO AM DEPTH | 140 | (no function) | — |
| 14 | LFO KEY SYNC | 141 | BATTERY CHECK | — |
| 15 | P MOD SENS | 143 | CARTRIDGE SAVE | — |
| 16 | A MOD SENS | op 14 | CARTRIDGE LOAD | — |
| 17 | OSC MODE / OSC KEY SYNC | op 17 / 136 | WHEEL RANGE | 70 |
| 18 | FREQ COARSE | op 18 | WHEEL PITCH | 71 bit 0 |
| 19 | FREQ FINE | op 19 | WHEEL AMP | 71 bit 1 |
| 20 | OSC DETUNE | op 20 | WHEEL EG BIAS | 71 bit 2 |
| 21 | EG RATE 1–4 | op 0–3 | FOOT RANGE | 72 |
| 22 | EG LEVEL 1–4 | op 4–7 | FOOT PITCH | 73 bit 0 |
| 23 | BREAK POINT | op 8 | FOOT AMP | 73 bit 1 |
| 24 | KEY SCALE CURVE L/R | op 11 / 12 | FOOT EG BIAS | 73 bit 2 |
| 25 | KEY SCALE DEPTH L/R | op 9 / 10 | BREATH RANGE | 74 |
| 26 | RATE SCALING | op 13 | BREATH PITCH | 75 bit 0 |
| 27 | OUTPUT LEVEL | op 16 | BREATH AMP | 75 bit 1 |
| 28 | KEY VELOCITY | op 15 | BREATH EG BIAS | 75 bit 2 |
| 29 | P EG RATE 1–4 | 126–129 | AFTER RANGE | 76 |
| 30 | P EG LEVEL 1–4 | 130–133 | AFTER PITCH | 77 bit 0 |
| 31 | KEY TRANSPOSE | 144 | AFTER AMP | 77 bit 1 |
| 32 | VOICE NAME | 145–154 | AFTER EG BIAS | 77 bit 2 |

"op k" means byte k of the selected operator's block.

**R-P2 LCD text.** The LCD shows exactly the text in §5.3 for every state. Each LCD update carries both lines plus the cursor (row, column, visible).

**R-P3 Data entry.** The slider, YES/NO, CC6, CC96 and CC97 act on the current page as in F-2.

**R-P4 EDITED.**
- **Becomes EDITED:** any change to the edit buffer's bytes 0–154, from any source. That includes the front panel, programmer, host automation, SysEx parameter change, a received or file-loaded single voice, VOICE INIT, EDIT RECALL, EG copy and randomise.
- **Never sets EDITED:** operator mask changes.
- **Becomes CLEAN:** only by voice selection (F-6) or store (F-7).

**R-P5 Voice selection** follows F-6 for every source.

**R-P6 Recall capture.** An EDITED edit buffer is copied to the recall buffer just before any of these replaces it:
- a voice selection;
- a received single voice;
- a file-loaded voice.

On first run, the recall buffer holds INIT VOICE.

**R-P7 Operator mask.** The mask resets to all-on whenever a voice is loaded into the edit buffer (F-6, F-10 9/10, F-12, F-13).

**R-P8 Display values** are identical in the LCD, the programmer and the host value strings, for the same parameter value.

**R-P9 Memory protect.**
- Internal protect blocks every write to the internal bank:
  - STORE to INT;
  - CARTRIDGE LOAD;
  - a bank-manager drop or load into internal;
  - a MIDI bank receive.
- Cartridge protect blocks every write to the cartridge:
  - STORE to CRT;
  - CARTRIDGE SAVE;
  - CARTRIDGE FORMAT;
  - a bank-manager drop into the cartridge.
- Inserting, creating or ejecting a cartridge is not a write.
- The edit buffer is never protected.
- A blocked write writes nothing and shows MEMORY PROTECTED.

**R-P10 INIT VOICE.** These are the values VOICE INIT, the default bank's empty slots and new cartridges use:
- **Every operator:**
  - EG rates 99/99/99/99, levels 99/99/99/0;
  - break point 0, depths 0, curves −LIN, rate scaling 0, AMS 0, KVS 0;
  - ratio mode, coarse 1, fine 0, detune 7.
- **Output levels:** OP1 99, OP2–OP6 0.
- **Pitch EG:** rates 99, levels 50.
- **Globals:**
  - algorithm 1, feedback 0, osc key sync on;
  - LFO speed 35, delay 0, PMD 0, AMD 0, LFO key sync on, wave triangle;
  - pitch mod sens 3, transpose 24.
- **Name:** `INIT VOICE`.

**R-P11 Stored voice.**
- The stored voice is taken at voice selection and at store.
- Later changes to the slot it came from (bank loads, drops) do not change it.

### 7.4 Synchronisation between edit paths

**R-S1 Four edit paths, one state.** These four paths change the same core state and produce identical results for the same parameter and value:
- programmer;
- front panel (button + YES/NO/slider);
- SysEx parameter change;
- host automation.

**R-S2 External changes.** Host automation, SysEx parameter changes and CC5:
- never change the panel mode, the page or the selected operator;
- end COMPARE;
- set EDITED (voice bytes);
- re-render the LCD if it shows the changed value.

**R-S3 Programmer changes** follow F-3.

**R-S4 Host round trip.** Changes that don't come from the host are reported to the host as parameter changes:
- front panel, SysEx, program change, voice loads, CC5;
- one notification per changed host parameter.

These reports are not applied back to the core a second time. When the host writes a parameter, the host's value wins at the start of the next block.

**R-S5 UI snapshot.** Whenever the UI connects (editor opens, page loads or reloads), it first receives the complete current state:
- all parameters, function parameters, panel globals and settings;
- the LCD and its cursor, the LED, the lamps;
- the bank names and cartridge status.

Only after that does it show values.

**R-S6 Update rates.** The UI receives state changes at up to 60 Hz, and meter levels at up to 30 Hz. A change from any source is visible in the UI within 100 ms.

### 7.5 Files and persistence

**R-D1 State.** State contents follow §4.8. One plugin instance = one state blob, so projects are self-contained.

**R-D2 Standalone files.**
- State lives in `state.bin` in one of:
  - `~/Library/Application Support/VDX/`
  - `%APPDATA%\VDX\`
  - `~/.config/VDX/`
- The directory is created if missing. `internal.syx` (4104 bytes) is written beside `state.bin` at each save.

**R-D3 Exact round trip.** Loading a voice or bank and saving or exporting it unchanged produces byte-identical data, including:
- out-of-range values;
- unused bits;
- non-printable name bytes.

Clamping applies only to effective values (§4.1).

**R-D4 File import.** Files are read as follows:
- **Larger than 1 MiB:** rejected.
- **Otherwise:** VDX scans for the first complete `F0 … F7` message that is a 163-byte voice or a 4104-byte bank. Everything else in the file is ignored.
- **A file of exactly 4096 bytes** is accepted as a headerless packed bank.
- **A checksum mismatch** loads the file and shows CHECK SUM ERROR!.
- **No usable message:** rejected, with FORMAT CONFLICT! and the bank-manager notice.

**R-D5 Web storage.**
- IndexedDB: database `vdx`, store `state`, key `current`. Device choices are stored under key `devices`.
- Two tabs: each saves on its own; the last write wins.

**R-D6 First run** (and state reset):
- the default bank (§15.3) in internal;
- no cartridge;
- the §4.2–§4.4 defaults;
- edit buffer, stored voice and recall buffer = INIT VOICE (internal slot 1), CLEAN.

### 7.6 Computer keyboard (`ui/src/keyboard.ts`)

**R-K1 Map.** Keys are matched by `KeyboardEvent.code` (physical position), never by character.

| Keys (QWERTY positions) | Function |
| --- | --- |
| A S D F G H J K L ; ' | C D E F G A B C D E F (white keys); the base is C3 = MIDI 60 |
| W E T Y U O P | C# D# F# G# A# C# D# |
| Z / X | Octave down / up. The base moves in 12s within 0…108; notes above 127 are not played. |
| C / V | Velocity −20 / +20. Default 100, clamped to 1–127. |
| Space (hold) | Sustain (CC64 127 / 0) |
| 1 / 2 (hold) | Pitch bend fully down / up; centre on release |
| 3, 4–8 | Mod wheel 0, then 25 / 51 / 76 / 102 / 127 |
| M or F9 | Toggle keyboard play on/off |

**R-K2 Behaviour.**
- **Note on and off:**
  - A note-on happens only if the key is not already held and `event.repeat` is false.
  - The note-off uses the note stored at key-down, even after an octave change.
- **Events that are never played:** events with Ctrl, Cmd, Alt/Option or Meta held, and events with `isComposing`.
- **Release all keys:** on window blur, on toggling play off, when a text field gains focus, and when the editor closes.
- **Capture:** while play is on, mapped keys are captured with `preventDefault`.
  - **Tab** is never captured.
  - **Space** is sustain, so focused buttons are activated with **Enter** while play is on. With play off, Space activates focused buttons as usual.
- **Text fields:** a focused text field suspends the mapping entirely.
- **Defaults:** play is on in the standalone and web builds, off in the plugin. M or F9 toggles it whenever the UI has keyboard focus and no text field is focused.

### 7.7 UI interaction

**R-U1 Controls.**
- Every programmer control shows its value permanently.
- **Entering a value:**
  - Double-click or start typing to enter a number. Enter commits, Esc reverts.
  - Out-of-range numbers are clamped; non-numbers revert.
- **Mouse:**
  - Wheel: ±1. Shift+wheel: ±10.
  - Vertical drag: 1 step per 4 px. Ctrl/Cmd+drag: 1 step per 16 px.
- Hover shows a tooltip with the parameter's name and range, and never changes a value.

**R-U2 Operator context menu** (right-click on an operator header):
- **Copy** puts the operator's 21 bytes on an in-app clipboard (per process).
- **Paste** writes it into this operator.
- **Swap with OP n** exchanges all 21 bytes of the two operators.
- **EG copy to OP n.**
- **Init operator:** the R-P10 operator values with output level 0.

**R-U3 Voice menu** (programmer header):
- **Init voice:** as FUNCTION 10, without confirmation.
- **Compare:** as EDIT/COMPARE; disabled while CLEAN.
- **Randomise:**
  - **Globals:** algorithm uniform 1–32, feedback uniform 0–7.
  - **Each operator:**
    - ratio mode, fine 0, detune uniform 4–10;
    - coarse drawn from {0 (×0.5), 1–8} with weight 3 for 1 and 2, and 1 for the others;
    - output level uniform 60–99 for carriers and 0–90 for modulators of the new algorithm;
    - EG rates uniform 30–99;
    - L1 99, L2 uniform 50–99, L3 uniform 0–99, L4 0;
    - KVS uniform 0–3;
    - other operator bytes as R-P10.
  - **Unchanged:** all other globals and the name.
  - **Afterwards:** operator mask all-on, EDITED.
  - **Seeding:** the generator is seeded from time in the app, and from a fixed seed in tests.

**R-U4 Shortcuts** (Alt = Option on macOS):

| Shortcut | Action |
| --- | --- |
| Alt+1…6 | Move keyboard focus to that operator panel and select the operator |
| Alt+Shift+1…6 | Toggle that operator |
| Alt+G | Focus the voice globals |
| Alt+B | Bank manager |
| Alt+, | Settings |
| Esc | Close the open overlay |

These are the same in every build. Shortcuts never play notes.

**R-U5 Accessibility.**
- Every control is reachable with Tab in visual order (front panel left to right, then programmer blocks left to right, top to bottom).
- Every control has an ARIA name equal to its legend or label.
- The visible focus ring is 2 px `--legend`.

**R-U6 Contrast.** These text/background pairs must each reach at least 4.5:1:
- programmer text (`--legend`) on `--programmer`;
- overlay text on its background;
- LCD ink (`--lcd-ink`) on `--lcd-bg`;
- badge text (`--body`) on `--carrier` and on `--modulator`.

Front-panel legends and button numbers are exempt, as a hardware replica, and keep their ARIA names. If a checked pair fails, adjust the foreground token by the smallest change that passes, and record it in `DECISIONS.md`.

### 7.8 Licensing and branding

**R-L1 JUCE licence.**
- JUCE 9 **Starter** (free) applies only while the licensee's revenue or funding over the previous 12 months is **at most $20,000**:
  - for an individual, that counts all revenue from use of JUCE;
  - for a company, it counts the entire company's and its affiliates' revenue.
- VDX is a personal project owned by Marco Funk. If the cap is exceeded, the options are Indie ($800 perpetual, ≤ $300k) or relicensing VDX as AGPLv3.
- The JUCE EULA (§2.3) forbids combining JUCE with anything that would force JUCE under an open-source licence. So nothing GPL or AGPL is ever linked or shipped.

**R-L2 Sources.**

| Source | Licence | Rule |
| --- | --- | --- |
| JUCE 9.x | Starter EULA | Git submodule, unmodified |
| VST3 SDK | MIT | Via JUCE |
| Dexed `source/msfa/*` except `tuning.cc/.h` | Apache-2.0 | Copy into `core/fm/third_party/msfa/`; keep headers; add a NOTICE file |
| Dexed `source/msfa/tuning.cc/.h` | No licence header | Do not copy. Replace with a standard-tuning stub (`midinote → logfreq`, as in Google's original MSFA `dx7note.cc`) |
| Dexed `source/engine/*`, GUI, `PluginProcessor`, `PluginData` | GPL-3.0 | Never copy. Fetched and built only by the optional oracle job into git-ignored `tools/oracle/build/` |
| hexter | GPL-2.0+ | Read-only reference for measured numbers; never copy code |
| ajxs DX7 v1.8 ROM disassembly | No licence; Yamaha firmware | Reference for behaviour and the short UI strings only; never copy code |
| r8brain-free-src | MIT | Resampler |
| Svelte | MIT | Shipped in the UI bundle |
| Vite, Vitest, Playwright, Catch2, Pamplejuce | MIT / Apache-2.0 / BSL-1.0 | Build and test only |
| Barlow Condensed, Oswald | SIL OFL 1.1 | Bundled as woff2 |
| pluginval | GPL-3.0 | Downloaded by CI as a separate tool; never in the repo, never linked |

**R-L3 Licence check (CI).** The build fails if any git-tracked file contains "GNU General Public License" or "GNU Affero". These are excluded from the check:
- the JUCE submodule;
- `tools/oracle/`;
- `docs/LICENSES.md`, which names licences.

**R-L4 `docs/LICENSES.md`** lists every shipped component with its licence:
- JUCE (Starter);
- VST3 SDK (MIT);
- MSFA (Apache-2.0, with NOTICE);
- r8brain (MIT);
- Svelte (MIT);
- the fonts (OFL).

It lists the build and test tools separately.

**R-L5 Branding.**
- "YAMAHA", the Yamaha logo and "DX7" never appear in the UI or in plugin metadata.
- "DX7" may appear only descriptively: in documentation, and in the bank manager's file-filter description ("DX7 SysEx files").

---

## 8. Edge cases

| Case | Behaviour |
| --- | --- |
| All six operators masked off | Silence. OPERATOR SELECT does nothing. The selected operator and LCD `OPn` stay as they were. |
| Selected operator gets masked off | It stays selected and editable. OPERATOR SELECT skips it from then on. |
| Voice source is CRT and the cartridge is ejected | The LCD and LED keep showing `CRT nn`. COMPARE still uses the stored voice. Selecting CRT memory gives NOT READY. |
| Program change 32–63 with no cartridge | Ignored. |
| STORE target equals the voice source | Overwrites the slot (subject to protect), then CLEAN. |
| Voice name with bytes outside 0x20–0x7E (imported) | Displayed as spaces. The bytes are kept until that character is edited (R-D3). |
| Very long or non-ASCII file names | Cartridge name: the first 64 characters of the file stem, shown with an ellipsis after 18 (§5.2). Export names are sanitised (F-12). |
| Long MIDI device names | One line, ellipsis in the middle, full name in a tooltip. |
| Velocity 127 with high KVS | The target level is clamped at 4095. In DAC mode a loud voice clips at the 15-bit stage (R-E9); that is intended. |
| 16 loud voices | The sum can exceed 1.0; the output hard-clips at ±1.0 (R-E10). |
| Note + transpose outside 0–127 | Folded by octaves (R-E6). |
| Same note from MIDI and the computer keyboard | Two voices; each note-off releases the oldest (R-E11). |
| Several sources for one controller | The last received value wins. While the user drags an on-screen wheel, incoming MIDI for that wheel is ignored until release. |
| Parameter-change numbers outside 0–155 / 64–77 | Ignored. |
| SysEx for another device number with omni off | Ignored. |
| A SysEx dump arriving during a file load or transmit | Processed in arrival order on the audio thread. No interleaving within one message. |
| Several messages within 2 s | The newest replaces the current one (F-2). |
| External change during a dialog | SysEx dumps and program changes cancel the dialog and then apply. Automation and SysEx parameter changes apply without cancelling it. |
| Host sample rate 22.05–192 kHz | Supported through the resampler. Rates outside that range are also run through the resampler, but not tested. |
| Host calls `prepare` again (rate or block change) | All state is kept and voices stop. |
| Editor closed while notes or wheels are held | Released and centred (F-15). |
| Two standalone instances, or two browser tabs | Independent engines. Saved state: the last writer wins (R-D5). |
| Corrupt or foreign state | Reset with a message (§4.8). |
| Disk or IndexedDB unavailable | VDX keeps running with a notice (F-15). |
| Web opened from a non-localhost origin without isolation | Error screen (§5.9). |
| Web MIDI denied, or SysEx denied | Keyboard-only, or MIDI without SysEx (§5.8). |
| Window or plugin resized below the minimum | The constrainer prevents it. Web: the scale stops at 50 % and the page scrolls. |

---

## 9. Non-functional requirements

### 9.1 Performance

- **Native benchmark** (`tools/bench`): 16 voices of the default BRASS voice, sustained for 10 s, in Mk I mode with the DAC on and the resampler to 48 kHz, on a GitHub-hosted `ubuntu-24.04` standard runner. Render time must be < 5 % of real time, median of 5 runs. The benchmark also reports µs per 128-sample block.
- **WASM benchmark:** the same benchmark compiled with emcc, run in Node 26 on the same runner. It must be < 25 % of real time.
- **UI:** a change from any source is visible within 100 ms (R-S6).

### 9.2 Platforms and formats

- **Desktop:**
  - macOS 13+ (universal binary): Standalone, VST3, AU.
  - Windows 10 22H2+ / 11 x64: Standalone, VST3.
  - Ubuntu 24.04 x64 with webkit2gtk-4.1: Standalone, VST3.
- **Web:**
  - Chrome and Edge (current), with Web MIDI.
  - Safari 17+, with the computer keyboard only.
  - Firefox may work but is not tested.

### 9.3 Plugin identity (never change the codes or bundle ID after the first saved DAW project)

```cmake
juce_add_plugin(VDX
    PRODUCT_NAME              "VDX"
    PLUGIN_NAME               "VDX"
    VERSION                   0.1.0
    COMPANY_NAME              "Marco Funk"
    COMPANY_COPYRIGHT         "© 2026 Marco Funk"
    BUNDLE_ID                 com.marcofunk.vdx
    PLUGIN_MANUFACTURER_CODE  Mfnk
    PLUGIN_CODE               Vdxf
    DESCRIPTION               "6-operator FM synthesizer"
    FORMATS                   Standalone VST3 AU
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         TRUE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES           Instrument Synth
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice
    NEEDS_WEBVIEW2            TRUE
    NEEDS_WEB_BROWSER         TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED  TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE)
```

- **Buses:** no audio input. The output is stereo (or mono if the host asks), carrying the mono engine signal on every channel.

### 9.4 Binding technical constraints

These were chosen by the owner. The tests rely on them.

- **Stack:**
  - Engine and front-panel logic: one C++17 static library, `vdx_core`, with no JUCE dependency, used unchanged by the plugin and (compiled with Emscripten) by the web build.
  - Desktop shell: JUCE 9 `AudioProcessor`. UI: `WebBrowserComponent`.
  - UI: **Svelte 5 + TypeScript**, one bundle for all builds.
- **Threading.** `vdx_core` owns all state: the edit buffer, banks, function parameters, panel globals and front-panel state. Only the audio thread touches it.
  - Other threads talk to it through fixed-size, lock-free, allocation-free queues: commands in, events out.
  - Commands and events are applied in order. A command posted before a `render()` call takes effect in that call.
- **Public API** (signatures binding; `Command`, `Event`, `StateBlob` contents are latitude):
```cpp
namespace vdx {
struct Settings { EngineMode engine = EngineMode::MkI; bool dac = true; bool hostRate = false;
                  bool classicVelocity = false; };
class Instrument {
public:
  explicit Instrument(double hostSampleRate, int maxBlock);   // allocates everything here
  void prepare(double hostSampleRate, int maxBlock);          // may allocate; not on audio thread
  bool post(const Command&);                                  // one producer thread
  bool pollEvent(Event&);                                     // one consumer thread
  void midi(const uint8_t* bytes, int len, int sampleOffset); // audio thread, incl. SysEx
  void render(float* L, float* R, int n);                     // audio thread, no alloc/lock/log
  int  latencySamples() const;
  void saveState(StateBlob&) const;  bool loadState(const StateBlob&);
};
}
```
  MIDI channel and omni are panel globals, not `Settings`.

### 9.5 Other

- **Language:** English only.
- **Network:** no network access at runtime and no telemetry.
- **Assets:** no raster assets in the UI. Fonts are bundled; nothing loads from a CDN.

---

## 10. External services

None. VDX uses no APIs, accounts or keys. These local platform services have defined fallbacks:

| Service | Fallback |
| --- | --- |
| Audio devices (standalone) | JUCE device selector; no device → silent |
| Web Audio | §5.9 |
| Web MIDI | §5.8 |
| WebView2 / WebKitGTK | §5.10 |
| IndexedDB / file system | F-15 |

**For tests:**
- Web MIDI is mocked by a test double for `navigator.requestMIDIAccess` that records sent bytes and can inject messages.
- Audio is checked with an `AnalyserNode` tap exposed only in test builds.

---

## 11. Visual direction

- **Look:** a Mk I-era panel (dark brown case, green membrane buttons, violet and brown legends, salmon STORE), flat vector style, live 16×2 LCD and 2-digit LED.
- **Programmer:** a calmer, darker workspace in the same palette.

**Tokens.** Use these values as given, frozen in `ui/src/theme.css`. Final colour tuning against period photos is a human follow-up (§15.4).
```css
:root {
  --body:        #2B211B;
  --panel:       #4A392C;
  --programmer:  #3A2D24;
  --overlay:     #322720;
  --dx-green:    #63B39B;
  --dx-green-lit:#9BE3C9;
  --violet:      #9A86C4;
  --brown-legend:#C3926A;
  --salmon:      #EE8A76;
  --legend:      #E9E5CD;
  --lcd-bg:      #9BA48B;  --lcd-ink: #1C2216;  --lcd-off: #8E977F;
  --led-on:      #FF3B2F;  --led-off: #3A1512;
  --carrier:     #E9A15B;  --modulator: #6FA8DC;
}
```

**Lit buttons.** A lit button uses `--dx-green-lit`. A lit STORE uses `--salmon` with a 2 px `--legend` inner ring.

**Fonts.** Bundled woff2:
- **Barlow Condensed** 500/600 for legends and numbers, with `font-variant-numeric: tabular-nums`;
- **Oswald** 500 for the wordmark.

**LCD and LED.** Both are drawn as SVG:
- the LCD from `ui/src/lcd/font5x7.ts`, a 5×7 dot glyph table for 0x20–0x7E authored in the repo;
- the LED from `ui/src/led/segments.ts`.

---

## 12. Out of scope

- CLAP, AUv3 and iOS
- Undo/redo (EDIT RECALL and COMPARE cover the hardware's own safety nets)
- A "tracker" computer-keyboard layout
- Public web hosting, and a non-threaded web build
- MIDI learn, MPE, microtuning or scala files, and multi-timbral operation
- DX7 II-era features: performances, unison, fractional scaling, second LFO and so on
- Effects such as chorus or reverb
- Bundling factory ROM or community banks (`tools/fetch_banks.sh` downloads them for local testing only)
- Localisation
- Touch-optimised layouts, and layouts below 640 × 500
- A cartridge that writes back to its file automatically
- Signed or notarised installers

---

## 13. Acceptance criteria

Every criterion is an automated test whose name contains its AC ID. Unless a criterion says otherwise:
- **Core criteria** (Catch2) run natively and under WASM (Node 26, the same test binary compiled with emcc), with identical expectations.
- **UI criteria** run with Playwright against the web build in Chromium and WebKit on Linux.
- **Keyboard criteria** run in Vitest.

**Reference test voice.** Where a criterion says "test voice", it means: INIT VOICE (R-P10), algorithm 32, OP1 only (OP2–6 output level 0), velocity 100, note 60, Mk I mode with the DAC off, measured on the internal-rate engine output before the output stage.

### 13.1 Sound engine

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-E1.1 | Host rates 44.1, 48 and 96 kHz | A note is rendered | L and R are identical, and the measured onset delay equals `latencySamples()` ± 1 sample |
| AC-E1.2 | "Render at host rate" on at 48 kHz | T1-2 is repeated at qrate 0, 20, 40, 60 | Slopes are within ± 2 % of the formula |
| AC-E1.3 | A web build whose AudioContext refuses 49,096 Hz (test stub throws) | A key is pressed | Output is non-silent (analyser RMS > −60 dBFS) |
| AC-E1.4 (goldens) | 32 algorithm renders (INIT VOICE, all operators OL 99, OP2 coarse 2) and the 9 non-INIT default-bank voices at velocities 30/100/127 (note 60, 0.8 s held + 0.2 s released, Mk I, DAC on) | Rendered natively on all three OSes and in WASM | The int32 DAC tap stream at the internal rate is bit-identical to the committed goldens. Goldens are regenerated only in the same commit as an intended change. |
| AC-E3.1 (T1-11) | Test-only flags turn off Mk I frequency quantisation and the DAC; seed 1; 20 random voices with FB 0 and modulator OL ≤ 80, using algorithms whose carrier count C ∈ {1, 2, 4}; notes 48/60/72 | Rendered in Mk I and Modern | RMS differs by < 0.1 dB, and partials above −40 dB agree within 1 dB |
| AC-E3.2 | Generated tables | Inspected | `exptab[0] = 8192`, `exptab[1023] = 4099`, `logsin[1023] = 0`, `logsin[0] = 10597` |
| AC-E3.3 (T1-1) | Test voice with OP1 fixed 1 kHz (coarse 3, fine 0), R1 99, L1 99 | OL is varied 0–99 | For OL 20–99, each step = 0.75 ± 0.05 dB (RMS of 0.5 s steady state). For OL 0–19, the internal level follows the LUT exactly. |
| AC-E4.1 (T1-2) | Test voice with L1 99, L2 0, rate scaling 0 | The R2 decay is run for each rate 0…99 | The slope over the first 20 dB, measured on internal `L`, is within ± 2 % of `0.2819 · 2^(qrate/4) · (1 + 0.25·(qrate mod 4))` dB/s |
| AC-E4.2 (T1-3) | qrate 0 | 2^16 samples are run | `L` changes on average once per 4096 samples, and the gate masks match R-E4 exactly for qrate & 3 = 0…3 |
| AC-E4.3 (T1-4) | The test voice at every rate | Attack 0→99 is compared with decay 99→0 | Attack is faster. Rate-99 attack reaches within 0.1 dB of target in ≤ 200 samples. |
| AC-E4.4 | Segments with equal start and target, at rates 0, 50, 76, 77, 99 | Run | Hold lengths equal `round(statics[r] × 49096/44100)` or `20 × (99 − r)` respectively, ± 1 sample |
| AC-E5.1 (T1-5) | L 99, OL 99, velocity 100 | Rendered | Peak level = 3840 counts; the level never goes below 16 |
| AC-E5.2 | KVS 7 | Velocity 100 vs 127 vs 30 | Velocity 100 equals the KVS 0 level ± 1 count; 127 is louder; 30 is quieter. With classic scaling on, velocity 127 renders identically to velocity 100 with it off. |
| AC-E5.3 | Each curve code 0–3 with depth 99, BP 39 | Notes BP ± 24 are played | The level offset matches R-E5 exactly (in counts) |
| AC-E6.1 (T1-9) | Mk I mode, any voice and controller state (100 random cases, seed 1) | Rendered | Every computed logfreq is a multiple of 2^14 |
| AC-E6.2 | Modern mode | Ratio coarse 1 at note 69; fixed coarse 2 fine 0; master tune 127 | 440 Hz ± 0.1 %; 100 Hz ± 0.1 %; +73.83 cents ± 0.5 |
| AC-E6.3 | Transpose 48 | Note 120 is played | It sounds at note 120 (144 folded down 2 octaves) |
| AC-E6.4 | Bend range 12, step 2 | Bend is swept | Pitch takes only multiples of 2 semitones |
| AC-E7.1 (T1-6) | LFO speeds 0, 25, 50, 63, 75, 99 | Measured | The period is within ± 2 % of the MSFA table |
| AC-E7.2 (T1-7) | Pitch EG levels 0 / 17 / 50 / 82 / 99 | Held | −48 / −12.375 / 0 / +12 / +47.625 semitones ± 0.05 |
| AC-E7.3 | S/H wave; LFO key sync on | 16 steps; a new note | The values follow the LCG; the phase restarts at 2^31 − 1 |
| AC-E8.1 (T1-10) | The `FmAlgorithm` table | Carriers are derived | They equal the R-E8 list |
| AC-E8.2 (T1-8) | Test voice in Modern mode, OP1 FB 0…7 | Measured | FB 0: THD < −60 dB. The 2nd harmonic changes monotonically with FB. FB 7: no line at fs/2 above −60 dB in the spectrum of `y[n] − y[n−2]`. |
| AC-E9.1 | Values ±0, ±2047, ±2048, ±4095, ±8191, ±16383, ±40000 | Quantised | Exactly as R-E9 |
| AC-E1.5 (T1-12) | Test voice in Modern mode with OP1 coarse 31, note 84 (f = 31 × 1046.50 Hz = 32,441.6 Hz) | The internal-rate spectrum is taken | The strongest peak lies at 49,096 − f = 16,654.4 Hz ± 0.2 % (the aliased image; no oversampling) |
| AC-E10.1 | 16 voices, OL 99, velocity 127, KVS 7 | Rendered | \|output\| ≤ 1.0. Volume 0 → silence. Volume 64 → gain (64/127)² ± 0.01 dB. |
| AC-E11.1 | 16 held notes | A 17th note | The earliest note-on is stolen. With one of 16 released, that one is stolen. |
| AC-E11.2 | Note 60 on twice | Note 60 off once | One voice is released (the older); the other keeps sounding |
| AC-E12.1 | Mono FINGERED | Legato and non-legato notes | Glide only on legato; EGs are not retriggered on legato. FULL TIME: glide on both. Releasing the top note returns to the held note. |
| AC-E12.2 | Poly RETAIN vs FOLLOW with sustain held | A new note | RETAIN: sustained voices keep their pitch. FOLLOW: they glide. |
| AC-E12.3 | Portamento time 50, CC65 on | Glide over 12 semitones | Duration = 12 / (2100 × 2^(−3.1)) s ± 5 %. CC65 off: no glide. |
| AC-E13.1 | Wheel range 99 with pitch, amp and EG bias assigned in turn; PMD 99, AMD 99 | Wheel 0 vs 127 | Pitch-mod depth, amplitude-mod depth and EG bias each change in the direction MSFA defines; all are 0 at wheel 0 |
| AC-E14.1 | A held note | Engine and DAC are toggled | The note keeps sounding. The output changes at the next block. |
| AC-E15.1 (T1-13) | Native test build with subnormal checks | A 60 s decay at host 44.1/48/96 kHz | No filter or resampler state value is ever subnormal |
| AC-E15.2 | A native build with an allocation hook | 10 s of notes, SysEx, commands and setting changes | Zero allocations, locks or log calls inside `render()`/`midi()` |
| AC-E15.3 | Host block sizes 1, 7, 64, 513, 8192, and n = 0 | The same MIDI is rendered | The DAC tap is bit-identical to the 64-sample reference; n = 0 renders nothing |

### 13.2 MIDI

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-M1.1 | Receive channel 3, omni off | Notes arrive on channels 1 and 3 | Only channel 3 sounds. With omni on, both sound. The plugin build behaves the same. |
| AC-M1.2 | — | Each R-M1 controller row is sent | The effect in that row occurs. CC5 64 → function 69 = 50. CC7 64 → master volume 64. CC6/96/97 act as the slider/YES/NO. |
| AC-M1.3 | With and without a cartridge | Program changes 0, 31, 32, 63, 64 are sent | INT 1, INT 32, CRT 1 (or ignored), CRT 32 (or ignored), ignored |
| AC-M2.1 | — | A 4104-byte bank arrives in 1-byte chunks with an interleaved F8; a 5000-byte SysEx; running-status notes | The bank loads; the 5000-byte message is ignored; all notes play |
| AC-M3.1 | AVAIL on | A front-panel edit, a programmer edit, a 1 s programmer drag, host automation, a received parameter change | 1 message; 1 message; ≤ 101 messages ending in the final value; 0; 0. With AVAIL off: 0 in every case. |

### 13.3 Front panel and flows

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-P1.1 | Each panel mode | Each of the 32 buttons is pressed | The page and parameter shown match R-P1 (core golden) and the lit button matches §5.4 (Playwright) |
| AC-P2.1 | Every EDIT and FUNCTION page and sub-page at its min, max and one mid value; every dialog; every message; the splash; PLAY CLEAN/EDITED/comparing | The LCD is read | Both lines and the cursor equal the committed golden matrix, derived by hand from §5.3, including every example string in §5.3 |
| AC-P3.1 | Each value page | Slider positions 0, 0.5, 1; CC6 0/64/127; YES/NO at the limits | The values follow F-2 and clamp at the limits |
| AC-P4.1 | CLEAN | Each R-P4 source changes a voice byte; then a mask toggle | Each makes EDITED; the mask toggle does not |
| AC-P6.1 | EDITED | A program change, then EDIT RECALL | The edits are back, EDITED |
| AC-P7.1 | OP3 masked off | Any voice load | The mask is all on |
| AC-P8.1 | Every parameter at min and max | The LCD, programmer and host value strings are read | The values are identical |
| AC-P9.1 | Protect on | Each R-P9 write path is tried | Nothing is written, and MEMORY PROTECTED is shown. With protect off, each succeeds. |
| AC-P10.1 | — | VOICE INIT | The 155 bytes equal R-P10 |
| AC-P11.1 | A voice selected from INT 5, then INT 5 overwritten by a bank load | COMPARE | The original INT 5 voice sounds |
| AC-F1.1 | Standalone and web | Start | The splash shows for 1.5 s ± 50 ms. Panel inputs during it are ignored; a MIDI note during it sounds. Web: the start card shows first, and its dismissing click activates nothing. |
| AC-F2.1 | Each cell of the F-2 tables | The input is given | The stated result occurs (core tests, one per cell) |
| AC-F2.2 | A pending dialog / a message | Another input | The dialog is cancelled or the message dismissed, then the input acts. Holding YES 1.5 s gives 1 + 10 ± 1 steps. |
| AC-F3.1 | PLAY | The programmer changes OP4 output level | Panel → EDIT page 27, LCD `OP4`, EDITED. A function change from EDIT → FUNCTION on its page. A pending dialog is cancelled. COMPARE ends. |
| AC-F4.1 | EDIT_NAME | All 39 character inputs; typing past character 10; FUNCTION | The characters follow the map; character 10 is overwritten; the panel returns to EDIT with no cursor. The programmer field turns `é😀` into `??` and truncates to 10. |
| AC-F5.1 | Page 31 armed | MIDI note 48, then note 60 | Transpose = 12 and the LCD reads `MIDDLE C = C 2`. The first note doesn't sound; the second does, 12 semitones lower than without transpose. |
| AC-F6.1 | EDITED on INT 3 | Press 7 in PLAY | INT 7 loads, CLEAN, LED 7, recall buffer = the old edits |
| AC-F7.1 | Selected memory CRT, protect CRT off | STORE, then button 9 | CRT 9 holds the edit buffer; voice source CRT 9; CLEAN; cartridge changed flag set |
| AC-F8.1 | Selected OP3 | STORE, then button 5 | OP5's R1–R4/L1–L4 equal OP3's. Pressing button 3 instead changes nothing. |
| AC-F9.1 | EDITED | EDIT/COMPARE, then host automation | The stored voice sounds while comparing; the automation ends compare and applies |
| AC-F10.1 | Each of pages 9, 10, 11, 15, 16, with and without a cartridge and protect | YES, YES | The R-P9 / F-10 result occurs and the stated message shows; NO at either step returns to the question |
| AC-F11.1 | — | MEMORY PROTECT INT, then YES, then NO, then button 7 | On, then off; the dialog ends and button 7 acts |
| AC-F12.1 | Bank manager | Load a 163-byte file; a 4104-byte file as cartridge and as internal (protected and not); a 4096-byte file; a WAV; a dropped file; New; Eject (changed); Export | Each behaves as F-12, with the stated notices and LCD messages |
| AC-F12.2 | Mocked MIDI with no reply | Request dump | "Waiting for bank…", then "No reply." after 10 s |
| AC-F13.1 | AVAIL on/off; omni on/off; device nibble match/mismatch; INT protect on/off | A voice, a bank, a parameter change and dump requests arrive | Accepted or ignored exactly per F-13, with the stated messages; replies on channel 1 |
| AC-F14.1 | EDIT with a STORE dialog pending | Program change 33 with a cartridge | The dialog is cancelled; CRT 2 is loaded; the selected memory is CRT |
| AC-F15.1 | Standalone state directory (test-overridden) | Edits, then quit within 1 s | `state.bin` and `internal.syx` hold the last edit. A failed write leaves the previous file intact. |
| AC-F15.2 | Web | Edit, reload | The same state returns. With IndexedDB disabled, the banner shows. |

### 13.4 Synchronisation, persistence, plugin

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-S1.1 | **Every** voice parameter 0–155 and function parameter 64–77, at random in-range values (seed 1) | Set via a core command, the front panel, SysEx parameter change (both pp encodings where they apply), and host automation (plugin-side native test using the real `AudioProcessor`) | The core state is identical across all four, and exactly one parameter-changed event is emitted per change |
| AC-S2.1 | EDIT page 8 | Host automation changes OP2 output level | The page, mode and selected operator are unchanged; EDITED |
| AC-S4.1 | Plugin test host | A voice selection | Each changed host parameter is notified once. Within the next 2 s, nothing more is notified or re-applied. |
| AC-S5.1 | A UI showing a non-default state | Page reload / editor reopen | Every displayed value, the LCD, LED and lamps equal the state before |
| AC-S6.1 | Web with mocked MIDI | CC1 64 is injected | The mod-wheel position updates within 100 ms |
| AC-D1.1 | Every field of §4.8 set to non-default values | `saveState` → `loadState` | All fields are equal |
| AC-D1.2 | A blob with a bad CRC, bad magic, truncated, or version 0 | Loaded | First-run state and `  STATE RESET   `. A zero-length blob: first-run state, no message. |
| AC-D3.1 | `banks/vdx-default.syx`, every file in `banks/external/` (skipped if absent), and a committed synthetic bank with out-of-range values, stray bits and name bytes 0x00/0x7F | Imported, then exported unchanged | Byte-identical |
| AC-D4.1 | Files of 0 bytes, 1 MiB + 1, a 4096-byte headerless bank, a file with junk before a voice dump, two concatenated banks, a bad checksum | Imported | Reject; reject; bank; voice; first bank; loaded + CHECK SUM ERROR! |
| AC-D6.1 | Empty storage | Start | The R-D6 state, including the §4.2 function defaults |
| AC-D7.1 | The default bank | Each slot plays note 60 at velocity 100 | Peak > −60 dBFS; names as §15.3 |
| AC-PL1 | VST3 on macOS, Windows, Linux (xvfb); AU on macOS | pluginval at strictness 5 | Pass |
| AC-PL2 | Plugin | Host parameters and programs are enumerated | 174 parameters with the §4.9 IDs, ranges, names, order and value strings; 32 programs named after the internal voices |
| AC-PL3 | Plugin test host | `getStateInformation` during playback, then `setStateInformation` into a new instance | Identical state; no audio dropout (no `render()` call exceeds its block's real-time budget) |

### 13.5 UI, keyboard, licensing, build, performance

| ID | Given | When | Then |
| --- | --- | --- | --- |
| AC-K1.1 | Play on | Each mapped key | The R-K1 note, octave, velocity, sustain, bend or wheel effect occurs |
| AC-K2.1 | Play on | Key repeat; octave change while held; blur; text focus; play toggled off; Ctrl+A; `isComposing`; Tab; Space | No repeat note-on; the original note is released; all are released; all are released; all are released; no note; no note; focus moves; sustain (and Enter activates the focused control) |
| AC-K2.2 | The plugin build | First open | Play is off; M turns it on |
| AC-U1.1 | Programmer controls (a sample: coarse, OL, algorithm, a toggle) | Wheel, Shift+wheel, 40 px drag, Ctrl+drag, typing 150, typing abc | +1, +10, +10, +2–3, clamped to the max, reverted |
| AC-U2.1 | — | Copy/paste, swap, EG copy, init operator | Bytes as R-U2 |
| AC-U3.1 | Seed 1, 100 randomisations | Inspect | Every value lies in its R-U3 range and the untouched fields are unchanged |
| AC-U4.1 | — | Each R-U4 shortcut | Its action occurs and no note sounds |
| AC-U5.1 | — | Tab from the first control to the last | Every interactive control receives focus once, in R-U5 order, with a visible ring. axe-core reports no missing names. |
| AC-U6.1 | `theme.css` | The contrast script runs | Every R-U6 pair ≥ 4.5:1 |
| AC-SC1 | Chromium on Linux | Screenshots at 50, 100, 200 % in PLAY, EDIT (page 21), FUNCTION (page 17), bank manager, settings | Differences from the committed baselines ≤ 0.1 % of pixels. The baselines are generated by the implementer and committed. |
| AC-SC2 | Web | Before the gesture; served without COOP/COEP; `requestMIDIAccess` rejected; granted without SysEx | The start card shows; the isolation error screen shows; Settings shows the §5.8 "MIDI not available" text; Settings shows "SysEx not permitted" and Send bank is disabled |
| AC-SC3 | `ui/dist` | Inspected | No png/jpg/gif/webp/bmp files and no `data:image/` raster URIs |
| AC-L1.1 | The repo | The licence check runs; then a test plants "GNU General Public License" in a first-party file | Passes; fails |
| AC-L4.1 | `docs/LICENSES.md` | Inspected | Contains every R-L4 entry |
| AC-L5.1 | `ui/src`, `plugin/`, built plugin metadata | Grep for `YAMAHA` and `DX7` | No hits, except the bank-manager file-filter text and `docs/` |
| AC-B1 | A clean checkout | The README commands run on macOS, Windows, Ubuntu, and the emcc build | All targets build and all tests pass |
| AC-B2 | `npm run web` | The page is opened (Chromium, WebKit) | It is served on `http://localhost:5173` with COOP/COEP headers. A computer-keyboard key gives analyser RMS > −60 dBFS. In Chromium, a mocked Web MIDI note does the same. |
| AC-N1.1 | §9.1 benchmarks | Run in CI | Native < 5 % and WASM < 25 % of real time; µs per block reported in the CI log |

### 13.6 Traceability

| Rules / flows | Covered by |
| --- | --- |
| R-E1, R-E2 | AC-E1.1–E1.5, AC-E5.1 |
| R-E3 – R-E15 | AC-E3.x – AC-E15.x (matching numbers); R-E9 also AC-E1.4; R-E14 also AC-E3.1 |
| R-M1 – R-M3 | AC-M1.x – AC-M3.x |
| R-P1 – R-P11 | AC-P1.1 – AC-P11.1 (R-P5 → AC-F6.1) |
| R-S1 – R-S6 | AC-S1.1, AC-S2.1 + AC-F9.1, AC-F3.1, AC-S4.1, AC-S5.1, AC-S6.1 |
| R-D1 – R-D6 | AC-D1.x, AC-F15.1, AC-D3.1, AC-D4.1, AC-F15.2, AC-D6.1 |
| R-K1, R-K2 | AC-K1.1, AC-K2.x |
| R-U1 – R-U6 | AC-U1.1 – AC-U6.1 |
| R-L1 – R-L5 | AC-L1.1 (R-L1–L3), AC-L4.1, AC-L5.1 |
| F-1 – F-15 | AC-F1.1 – AC-F15.2 |
| §4.9, §5, §9.1, §15.3 | AC-PL2, AC-SC1–SC3 + AC-F12.x, AC-N1.1, AC-D7.1 |

---

## 14. Implementer's latitude

These are yours to decide. Record notable choices in `DECISIONS.md`. Where a recommendation is given, you may follow it or replace it.

- **Internal architecture beyond §9.4:**
  - **Queues:** sizes and overflow policy. Recommended: 4096 entries each; coalesce slider moves per parameter on the producer side; when the event queue is full, drop meter, then LCD, then lamp events first, and count drops.
  - **Messages:** `Command`/`Event`/`StateBlob` structures. Recommended: ≤ 32-byte PODs, with bulk payloads in preallocated slot pools.
  - **Host state save:** how state is snapshotted for a host save on the message thread. Recommended: a double-buffered snapshot published by the audio thread.
  - **UI bridge.** Recommended:
    - desktop: JUCE `WebBrowserComponent` with `withNativeIntegrationEnabled`, a resource provider serving `ui/dist` from BinaryData, a native function `vdxCommand(json)`, and an event `vdxEvents` (batched);
    - web: `AudioWorkletNode.port` with transferable typed arrays.
- **State blob field layout** (documented in `docs/STATE.md`).
- **Bit-exactness technique.** Recommended:
  - commit generated integer tables, with a test that regenerates them;
  - compile with `-ffp-contract=off` and no fast-math;
  - keep libm transcendental calls out of anything that feeds the integer signal path at runtime.
- **Repository layout.** Recommended:

  ```
  vdx/
    CMakeLists.txt            # options VDX_BUILD_PLUGIN / VDX_BUILD_WEB / VDX_BUILD_TESTS
    core/include/vdx/ core/fm/ core/fm/third_party/msfa/ core/fm/tables/ core/sysex/ core/panel/
    third_party/JUCE (submodule) third_party/r8brain/
    tests/ (tier1/ tier2/ golden/)  plugin/  web/ (worklet.cpp, build.sh)
    ui/ (src/{panel,programmer,lcd,led,bank,settings}, bridge.ts, keyboard.ts, theme.css, fonts/)
    banks/ (vdx-default.syx, default/*.json, external/ git-ignored)
    tools/ (make_default_bank.py, fetch_banks.sh, syx_inspect.py, bench/, oracle/, capture/)
    docs/  .github/workflows/
  ```
- **Order of work.** Recommended, with gates:
  1. engine (Tier 1 green);
  2. SysEx, state and front-panel logic (panel goldens green);
  3. WASM (goldens bit-exact);
  4. plugin (pluginval);
  5. UI (Playwright);
  6. web and performance.
- **Build system details.** Pamplejuce or your own CMake; CI structure.
- **Visual details not fixed in §5 and §11:**
  - exact spacing inside blocks;
  - algorithm-diagram node layout (derived from `FmAlgorithm` by a consistent rule);
  - EG and level-scaling graph axis mapping;
  - legend wrapping and size (≥ 7 px) where text does not fit 29 px;
  - icon shapes, hover styles, and tooltip wording beyond name + range.
- **The eight default-bank voices' programming** (sound quality is not gated).
- **The optional oracle report:** `tools/oracle/` fetches and builds Dexed (GPL, never linked) and publishes a per-patch log-spectral distance report in CI. It is not a gate.

---

## 15. Delivery

### 15.1 How it is run

The README documents:
- configure, build and test for desktop with CMake ≥ 3.25 (options `VDX_BUILD_PLUGIN`, `VDX_BUILD_WEB`, `VDX_BUILD_TESTS`);
- the WASM build;
- `npm run web`.

**CI** (GitHub Actions):
- the matrix covers macOS (universal), Windows (MSVC), Ubuntu 24.04 (webkit2gtk-4.1) and wasm;
- it runs every §13 criterion, the licence check and the benchmarks;
- signing and notarisation steps are present but skipped unless their secrets exist.

### 15.2 Web (local hosting)

- `web/build.sh` runs `emcc -O3 -msimd128 -sAUDIO_WORKLET -sWASM_WORKERS -sENVIRONMENT=web,worker,worklet` over `vdx_core` plus `web/worklet.cpp`, producing `ui/public/vdx.wasm` and its loader.
- `npm run web` starts the Vite dev server on `http://localhost:5173`. It sets `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`, also for `vite preview`.
- The page states are as §5.9.

### 15.3 Default bank

`banks/vdx-default.syx` is generated by `tools/make_default_bank.py` from readable JSON in `banks/default/*.json`:

| Slot | Content |
| --- | --- |
| 1 | `INIT VOICE` |
| 2–9 | Eight original voices: `E.PIANO`, `BELL`, `BASS`, `BRASS`, `ORGAN`, `MARIMBA`, `STRINGS`, `PAD` |
| 10–32 | `INIT VOICE` |

`tools/fetch_banks.sh` downloads external banks into git-ignored `banks/external/` for local testing. Tests that need them skip when the folder is empty.

### 15.4 Human follow-ups (not agent acceptance)

| Item | Why it needs a person |
| --- | --- |
| Hardware A/B with a DX7/TX7: AM depth curve, rates 77–99, detune in Hz, portamento curve, voice stealing, controller range mapping, the six community-reported Dexed discrepancies | Needs the instrument; tables are replaced via `tools/capture/` |
| Optional Plogue chipsynth OPS7 comparison | Purchase and manual capture |
| Real Safari, real Web MIDI devices, real audio devices in the standalone, Web MIDI in Firefox | CI has no such devices or browsers |
| Final colour hex values against period photos | Visual judgement |
| Signed/notarised installers | Apple Developer ID / Windows signing credentials |
| Public web hosting | Needs a host that sets COOP/COEP |
| Trade-dress and name check before public release; replacing `com.marcofunk.vdx` with an owned reverse domain and re-checking the four-character codes | Legal judgement and ownership |

### 15.5 Sources

- **Hardware and engine:**
  - Ken Shirriff's DX7 reverse-engineering series (righto.com: OPS chip, exp ROM, log-sine ROM, algorithm ROM, DAC).
  - MSFA wiki: Dx7Hardware, Dx7Envelope (github.com/google/music-synthesizer-for-android).
  - Dexed `source/msfa/*` (github.com/asb2m10/dexed).
  - hexter tables (github.com/smbolton/hexter).
  - ajxs DX7 v1.8 ROM disassembly (github.com/ajxs/yamaha_dx7_rom_disassembly): the source of the button map, LCD strings, operator-mask bit order, naming character map, and transpose/break-point note display.
- **Formats and input:** DX7 SysEx format (Macpherson 1993); chipple.net DX7 function-mode page; DX7s MIDI implementation chart; MDN `KeyboardEvent.code`.
- **Toolchain and licences:** JUCE 9 EULA; JUCE CMake API; VST3 SDK (MIT); Pamplejuce; pluginval; Emscripten Wasm Audio Worklets; r8brain-free-src.

# VDX state blob

The state blob holds everything SPEC §4.8 saves: banks, cartridge, edit buffer,
stored voice, recall buffer, voice source, selected memory, function
parameters, panel globals and the saved settings. Panel mode, pages, dialogs
and the operator mask are not saved (every load starts in PLAY with all
operators on).

It is written by `vdx::state::write()` and read by `vdx::state::read()`
(`core/sysex/state.h`), through `vdx::Core::serialize()` / `deserialize()`.
The same blob is the plugin's host state, the standalone `state.bin` and the
web IndexedDB value (`vdx` / `state` / `current`).

## Layout (version 1)

All multi-byte integers are little endian. Flags are 0 or 1. Total length
**9155 bytes** (`state::blobBytesV1()`; the maximum is `kMaxStateBytes` =
16384).

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `"VDX1"` |
| 4 | 2 | Version, u16 = 1 |
| 6 | 2 | Reserved, 0 |
| **8** | | **Prefix (banks, buffers, function parameters)** |
| 8 | 4096 | Internal bank: 32 packed voices (SPEC §4.6), slot 1 first, 128 bytes each |
| 4104 | 1 | Cartridge present |
| 4105 | 1 | Cartridge changed flag |
| 4106 | 1 | Cartridge name length, 0–64 |
| 4107 | 64 | Cartridge name bytes (UTF-8 file stem), zero padded |
| 4171 | 4096 | Cartridge bank, packed like the internal bank (zeros when absent) |
| 8267 | 284 | Edit buffer (voice record, below) |
| 8551 | 1 | EDITED flag |
| 8552 | 284 | Stored voice (voice record) |
| 8836 | 284 | Recall buffer (voice record) |
| 9120 | 1 | Voice source bank: 0 INT, 1 CRT |
| 9121 | 1 | Voice source slot, 1–32 |
| 9122 | 14 | Function parameters 64–77, one byte each |
| **9136** | | **Rest (version 1 only)** |
| 9136 | 1 | Selected memory: 0 INT, 1 CRT |
| 9137 | 7 | Panel globals in `vdx::Global` order: master tune (0–127), MIDI channel (1–16), omni, SYS INFO AVAIL, protect internal, protect cartridge, master volume (0–127) |
| 9144 | 7 | Settings in `vdx::SettingId` order: engine (0 Mk I, 1 Modern), DAC, render at host rate, classic velocity, computer keyboard play, zoom (0 = fit, else 50–200 in steps of 10), operator order (0 OP1→OP6, 1 OP6→OP1) |
| 9151 | 4 | CRC32 (IEEE 802.3: reflected polynomial 0xEDB88320, init and final XOR 0xFFFFFFFF) of bytes 0–9150 |

**Voice record** (284 bytes): the 155 raw unpacked voice bytes exactly as
loaded (out-of-range values and non-printable name bytes included, R-D3), the
128-byte raw packed copy (§4.6), and one byte that is 1 when the raw packed
copy is valid (the voice has not been edited since it was unpacked; packing
then returns the copy unchanged).

Banks are stored packed, as on the hardware: a voice stored into a slot is
packed at that moment (its raw packed copy if it is unedited since unpacking,
otherwise the current values with unused bits 0).

## Reading

| Case | Result |
| --- | --- |
| Zero-length blob | First-run state (R-D6), no message (`Core::LoadResult::Empty`) |
| Magic not `"VDX1"`, CRC mismatch, shorter than 12 bytes, or version 0 | Discard, first-run state, `  STATE RESET   ` after the splash (`Reset`) |
| Version 1 with a length other than 9155 | As above (`Reset`) |
| Version 1 | Every field is loaded (`Ok`) |
| Version > 1 | CRC checked over the whole blob; only the prefix (offsets 8–9135) is loaded — banks, buffers, EDITED, voice source, function parameters — and the rest keeps its current values (`Ok`). A newer blob shorter than 8 + 9128 + 4 bytes is a `Reset`. |

Field values in a blob with a valid CRC are clamped to their ranges rather
than rejected (flags to 0/1, slot to 1–32, function parameters, globals and
zoom to their ranges). Raw voice bytes and bank bytes are never clamped.

## Compatibility rule for future versions

A version *n* > 1 blob must keep bytes 0–9135 exactly as above (magic,
version field, reserved field and the whole prefix) and may only change what
follows offset 9136. It must still end with the CRC32 of everything before it.
Version 1 readers then load its banks, buffers and function parameters, as
SPEC §4.8 requires.

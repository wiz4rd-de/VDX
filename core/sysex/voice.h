// Voice data (SPEC §4.1, §4.6, R-D3, R-P10): 155 raw unpacked bytes, the
// 128-byte raw packed copy, pack/unpack, effective values, INIT VOICE.
// Pure functions, no allocation; usable from any thread.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vdx::sysex {

constexpr int kVoiceBytes = 155;        // unpacked voice, indices 0..154
constexpr int kPackedVoiceBytes = 128;  // packed voice (§4.6)
constexpr int kBankVoices = 32;
constexpr int kPackedBankBytes = kBankVoices * kPackedVoiceBytes;  // 4096
constexpr int kNameLen = 10;
constexpr int kNameIndex = 145;

// One voice as the core keeps it. `raw` holds the bytes exactly as loaded
// (possibly out of range); `packed` is the raw packed copy, valid while the
// voice has not been edited since it was unpacked (§4.6). All members are
// bytes, so the struct has no padding and can be compared with memcmp.
struct Voice {
  uint8_t raw[kVoiceBytes];
  uint8_t packed[kPackedVoiceBytes];
  uint8_t packedValid;  // 1 = packing returns `packed` unchanged
};

// Unpack a 128-byte packed voice. Every bit with a place in the unpacked form
// is kept; the whole packed voice is kept as the raw packed copy.
void unpackVoice(const uint8_t* packed128, Voice& out);
// A voice that arrived unpacked (single-voice dump, 155 bytes). No packed copy.
void voiceFromUnpacked(const uint8_t* raw155, Voice& out);
// Pack: the raw packed copy when valid, else packCurrent(raw).
void packVoice(const Voice& v, uint8_t* out128);
// Pack the current values with unused bits 0. Single-field bytes keep the raw
// byte; bit fields are packed from effective (clamped) values.
void packCurrent(const uint8_t* raw155, uint8_t* out128);

// Write one byte (an edit). Returns true if the byte changed; a change
// invalidates the raw packed copy.
bool setByte(Voice& v, int index, uint8_t value);

// INIT VOICE (R-P10).
void initVoiceRaw(uint8_t* raw155);
void initVoice(Voice& out);
// INIT VOICE operator values (R-P10) with the given output level (R-U2 init operator: 0).
void initOperatorRaw(uint8_t* op21, int outputLevel);

// Effective values (§4.1): raw clamped to the parameter range.
uint8_t effectiveByte(int index, uint8_t raw);
void effectiveVoice(const uint8_t* raw155, uint8_t* eff155);

// Name display: bytes outside 0x20..0x7E display as a space.
inline char displayChar(uint8_t b) { return (b >= 0x20 && b <= 0x7E) ? char(b) : ' '; }
void displayName(const uint8_t* raw155, char* out10);
// Name of a packed voice (bytes 118..127).
void packedName(const uint8_t* packed128, char* out10);

// Carrier operators of an algorithm 0..31 (MSFA FmAlgorithm table): bit op-1.
uint8_t carrierMask(int algorithm);

}  // namespace vdx::sysex

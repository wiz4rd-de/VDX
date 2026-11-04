// State blob (SPEC §4.8, R-D1, R-D6). Layout documented in docs/STATE.md.
// Pure functions over a plain struct, no allocation; usable from any thread.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sysex/voice.h"

namespace vdx::state {

constexpr uint16_t kVersion = 1;
constexpr int kCrtNameMax = 64;
constexpr int kFunctionCount = 14;  // 64..77
constexpr int kGlobalCount = 7;     // vdx::Global
constexpr int kSettingCount = 7;    // vdx::SettingId

// Everything §4.8 saves. All members are bytes (no padding), so two instances
// can be compared with memcmp.
struct Persist {
  // ---- prefix: banks, buffers, function parameters (read from newer versions too)
  uint8_t intBank[sysex::kPackedBankBytes];
  uint8_t crtPresent;
  uint8_t crtChanged;
  uint8_t crtNameLen;                 // 0..64
  char crtName[kCrtNameMax];          // not NUL-terminated
  uint8_t crtBank[sysex::kPackedBankBytes];
  sysex::Voice edit;
  uint8_t edited;
  sysex::Voice stored;
  sysex::Voice recall;
  uint8_t srcBank;                    // 0 INT, 1 CRT
  uint8_t srcSlot;                    // 1..32
  uint8_t fn[kFunctionCount];         // function parameters 64..77
  // ---- rest (version 1 only)
  uint8_t selMem;                     // 0 INT, 1 CRT
  uint8_t globals[kGlobalCount];      // vdx::Global order
  uint8_t settings[kSettingCount];    // vdx::SettingId order
};

// Byte offsets of the version-1 layout (docs/STATE.md).
constexpr size_t kHeaderBytes = 8;  // "VDX1", u16 version, u16 reserved
size_t prefixBytes();               // length of the prefix section
size_t restBytes();                 // length of the version-1 rest section
size_t blobBytesV1();               // total length of a version-1 blob (incl. CRC)

// Writes a version-1 blob. Returns bytes written, 0 if cap is too small.
size_t write(const Persist& s, uint8_t* dst, size_t cap);

enum class ReadResult { Ok, Newer, Reset, Empty };
// Checks magic, CRC, version and length without decoding.
ReadResult validate(const uint8_t* data, size_t len);
// Decodes into `out`. On Ok every field is replaced; on Newer only the prefix
// (banks, buffers, function parameters) is replaced and the rest of `out` is
// left as it was; on Reset / Empty `out` is untouched. Field values are
// clamped to their ranges.
ReadResult read(const uint8_t* data, size_t len, Persist& out);

}  // namespace vdx::state

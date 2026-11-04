// SysEx messages (SPEC §4.7), the F-13 receive filter, the R-D4 file scanner
// and CRC32. Pure functions, no allocation, no globals: usable from any
// thread (audio thread, message thread, web worker).
#pragma once

#include <cstddef>
#include <cstdint>

#include "sysex/voice.h"

namespace vdx::sysex {

constexpr int kVoiceMsgBytes = 163;   // F0 43 0n 00 01 1B + 155 + sum + F7
constexpr int kBankMsgBytes = 4104;   // F0 43 0n 09 20 00 + 4096 + sum + F7
constexpr int kParamMsgBytes = 7;     // F0 43 1n gp pp dd F7
constexpr int kDumpReqBytes = 5;      // F0 43 2n ff F7
constexpr size_t kMaxFileBytes = 1u << 20;  // R-D4: larger files are rejected
constexpr uint8_t kYamaha = 0x43;

// (-sum(data)) & 0x7F
uint8_t checksum(const uint8_t* data, int n);

// Builders. `device` = device nibble 0..15 (MIDI channel - 1). Data bytes are
// masked to 7 bits. Each returns the message length.
int buildVoiceDump(const uint8_t* raw155, int device, uint8_t* out163);
int buildBankDump(const uint8_t* packed4096, int device, uint8_t* out4104);
// group 0 = voice (param 0..155), 2 = function (64..77).
int buildParamChange(int device, int group, int param, int value, uint8_t* out7);
// format 0 = voice, 9 = bank.
int buildDumpRequest(int device, int format, uint8_t* out5);

enum class MsgKind : uint8_t { None = 0, Voice, Bank, ParamChange, DumpRequest };

struct Parsed {
  MsgKind kind = MsgKind::None;
  int device = 0;           // n
  bool checksumOk = true;   // voice / bank
  const uint8_t* data = nullptr;  // voice: 155 unpacked bytes; bank: 4096 packed bytes (points into msg)
  int group = 0;            // parameter change: g
  int param = 0;            // parameter change: (pp << 7) | p
  int value = 0;            // parameter change: d
  int format = 0;           // dump request: 0 or 9
};

// Classifies one complete SysEx message (F0 ... F7). Only manufacturer 0x43
// and messages whose length matches their format are recognised; anything
// else gives MsgKind::None.
bool parseMessage(const uint8_t* msg, int len, Parsed& out);

// F-13.1: accepted only when SYS INFO AVAIL is on, the manufacturer is 0x43
// and the device nibble equals channel - 1 (any nibble while omni is on).
bool acceptReceived(const Parsed& p, bool sysInfoAvail, bool omni, int channel1to16);

// R-D4 file import.
enum class ScanKind : uint8_t { Reject = 0, Voice = 1, Bank = 2 };
struct ScanResult {
  ScanKind kind = ScanKind::Reject;
  bool checksumOk = true;
  int dataLen = 0;          // 155 (voice, unpacked) or 4096 (bank, packed)
  uint8_t data[kPackedBankBytes] = {};
};
// Larger than 1 MiB: reject. Exactly 4096 bytes: headerless packed bank.
// Otherwise the first complete F0..F7 message that is a 163-byte voice or a
// 4104-byte bank. Anything else: reject.
ScanKind scanFile(const uint8_t* file, size_t len, ScanResult& out);

// CRC32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF).
uint32_t crc32(const uint8_t* data, size_t n);

}  // namespace vdx::sysex

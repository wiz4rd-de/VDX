#include "sysex/sysex.h"

#include <cstring>

namespace vdx::sysex {

namespace {

struct CrcTable {
  uint32_t t[256];
  constexpr CrcTable() : t() {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
  }
};
constexpr CrcTable kCrc;

bool isVoiceHeader(const uint8_t* m) {
  return m[0] == 0xF0 && m[1] == kYamaha && (m[2] & 0xF0) == 0x00 && m[3] == 0x00 && m[4] == 0x01 && m[5] == 0x1B;
}
bool isBankHeader(const uint8_t* m) {
  return m[0] == 0xF0 && m[1] == kYamaha && (m[2] & 0xF0) == 0x00 && m[3] == 0x09 && m[4] == 0x20 && m[5] == 0x00;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = kCrc.t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

uint8_t checksum(const uint8_t* data, int n) {
  int sum = 0;
  for (int i = 0; i < n; ++i) sum += data[i];
  return uint8_t((-sum) & 0x7F);
}

int buildVoiceDump(const uint8_t* raw155, int device, uint8_t* o) {
  o[0] = 0xF0;
  o[1] = kYamaha;
  o[2] = uint8_t(device & 0x0F);
  o[3] = 0x00;
  o[4] = 0x01;
  o[5] = 0x1B;
  for (int i = 0; i < kVoiceBytes; ++i) o[6 + i] = uint8_t(raw155[i] & 0x7F);
  o[6 + kVoiceBytes] = checksum(o + 6, kVoiceBytes);
  o[7 + kVoiceBytes] = 0xF7;
  return kVoiceMsgBytes;
}

int buildBankDump(const uint8_t* packed, int device, uint8_t* o) {
  o[0] = 0xF0;
  o[1] = kYamaha;
  o[2] = uint8_t(device & 0x0F);
  o[3] = 0x09;
  o[4] = 0x20;
  o[5] = 0x00;
  for (int i = 0; i < kPackedBankBytes; ++i) o[6 + i] = uint8_t(packed[i] & 0x7F);
  o[6 + kPackedBankBytes] = checksum(o + 6, kPackedBankBytes);
  o[7 + kPackedBankBytes] = 0xF7;
  return kBankMsgBytes;
}

int buildParamChange(int device, int group, int param, int value, uint8_t* o) {
  o[0] = 0xF0;
  o[1] = kYamaha;
  o[2] = uint8_t(0x10 | (device & 0x0F));
  o[3] = uint8_t(((group & 0x1F) << 2) | ((param >> 7) & 0x03));
  o[4] = uint8_t(param & 0x7F);
  o[5] = uint8_t(value & 0x7F);
  o[6] = 0xF7;
  return kParamMsgBytes;
}

int buildDumpRequest(int device, int format, uint8_t* o) {
  o[0] = 0xF0;
  o[1] = kYamaha;
  o[2] = uint8_t(0x20 | (device & 0x0F));
  o[3] = uint8_t(format & 0x7F);
  o[4] = 0xF7;
  return kDumpReqBytes;
}

bool parseMessage(const uint8_t* m, int len, Parsed& out) {
  out = Parsed{};
  if (!m || len < 5 || m[0] != 0xF0 || m[len - 1] != 0xF7 || m[1] != kYamaha) return false;
  const int sub = m[2] >> 4;
  out.device = m[2] & 0x0F;
  switch (sub) {
    case 0x0:
      if (len == kVoiceMsgBytes && isVoiceHeader(m)) {
        out.kind = MsgKind::Voice;
        out.data = m + 6;
        out.checksumOk = checksum(m + 6, kVoiceBytes) == m[6 + kVoiceBytes];
        return true;
      }
      if (len == kBankMsgBytes && isBankHeader(m)) {
        out.kind = MsgKind::Bank;
        out.data = m + 6;
        out.checksumOk = checksum(m + 6, kPackedBankBytes) == m[6 + kPackedBankBytes];
        return true;
      }
      return false;
    case 0x1:
      if (len != kParamMsgBytes) return false;
      out.kind = MsgKind::ParamChange;
      out.group = (m[3] >> 2) & 0x1F;
      out.param = ((m[3] & 0x03) << 7) | (m[4] & 0x7F);
      out.value = m[5] & 0x7F;
      return true;
    case 0x2:
      if (len != kDumpReqBytes) return false;
      out.kind = MsgKind::DumpRequest;
      out.format = m[3];
      return true;
    default:
      return false;
  }
}

bool acceptReceived(const Parsed& p, bool sysInfoAvail, bool omni, int channel1to16) {
  if (p.kind == MsgKind::None || !sysInfoAvail) return false;
  return omni || p.device == ((channel1to16 - 1) & 0x0F);
}

ScanKind scanFile(const uint8_t* f, size_t len, ScanResult& out) {
  out.kind = ScanKind::Reject;
  out.checksumOk = true;
  out.dataLen = 0;
  if (!f || len == 0 || len > kMaxFileBytes) return out.kind;
  if (len == size_t(kPackedBankBytes)) {
    std::memcpy(out.data, f, kPackedBankBytes);
    out.dataLen = kPackedBankBytes;
    out.kind = ScanKind::Bank;
    return out.kind;
  }
  size_t i = 0;
  while (i < len) {
    if (f[i] != 0xF0) {
      ++i;
      continue;
    }
    // Find the end of this message: the first status byte after F0.
    size_t j = i + 1;
    while (j < len && f[j] < 0x80) ++j;
    if (j >= len) break;  // incomplete
    if (f[j] != 0xF7) {   // aborted by another status byte; resume there
      i = j;
      continue;
    }
    const size_t mlen = j - i + 1;
    const uint8_t* m = f + i;
    if (mlen == size_t(kVoiceMsgBytes) && isVoiceHeader(m)) {
      std::memcpy(out.data, m + 6, kVoiceBytes);
      out.dataLen = kVoiceBytes;
      out.checksumOk = checksum(m + 6, kVoiceBytes) == m[6 + kVoiceBytes];
      out.kind = ScanKind::Voice;
      return out.kind;
    }
    if (mlen == size_t(kBankMsgBytes) && isBankHeader(m)) {
      std::memcpy(out.data, m + 6, kPackedBankBytes);
      out.dataLen = kPackedBankBytes;
      out.checksumOk = checksum(m + 6, kPackedBankBytes) == m[6 + kPackedBankBytes];
      out.kind = ScanKind::Bank;
      return out.kind;
    }
    i = j + 1;
  }
  return out.kind;
}

}  // namespace vdx::sysex

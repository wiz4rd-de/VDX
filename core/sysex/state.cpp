#include "sysex/state.h"

#include <cstring>

#include "sysex/sysex.h"
#include "vdx/params.h"

namespace vdx::state {

namespace {

constexpr size_t kVoiceRecord = sysex::kVoiceBytes + sysex::kPackedVoiceBytes + 1;  // 284
constexpr size_t kPrefix = sysex::kPackedBankBytes + 3 + kCrtNameMax + sysex::kPackedBankBytes +
                           kVoiceRecord + 1 + kVoiceRecord + kVoiceRecord + 2 + kFunctionCount;
constexpr size_t kRest = 1 + kGlobalCount + kSettingCount;
constexpr size_t kBlobV1 = kHeaderBytes + kPrefix + kRest + 4;

struct Writer {
  uint8_t* p;
  size_t n = 0;
  void bytes(const void* src, size_t len) {
    std::memcpy(p + n, src, len);
    n += len;
  }
  void u8(uint8_t v) { p[n++] = v; }
  void voice(const sysex::Voice& v) {
    bytes(v.raw, sysex::kVoiceBytes);
    bytes(v.packed, sysex::kPackedVoiceBytes);
    u8(v.packedValid ? 1 : 0);
  }
};

struct Reader {
  const uint8_t* p;
  size_t n = 0;
  void bytes(void* dst, size_t len) {
    std::memcpy(dst, p + n, len);
    n += len;
  }
  uint8_t u8() { return p[n++]; }
  void voice(sysex::Voice& v) {
    bytes(v.raw, sysex::kVoiceBytes);
    bytes(v.packed, sysex::kPackedVoiceBytes);
    v.packedValid = u8() ? 1 : 0;
  }
};

uint8_t clampU8(int v, int lo, int hi) { return uint8_t(v < lo ? lo : (v > hi ? hi : v)); }
uint8_t bit(uint8_t v) { return v ? 1 : 0; }

uint32_t readU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void readPrefix(Reader& r, Persist& s) {
  r.bytes(s.intBank, sizeof s.intBank);
  s.crtPresent = bit(r.u8());
  s.crtChanged = bit(r.u8());
  s.crtNameLen = clampU8(r.u8(), 0, kCrtNameMax);
  r.bytes(s.crtName, kCrtNameMax);
  r.bytes(s.crtBank, sizeof s.crtBank);
  r.voice(s.edit);
  s.edited = bit(r.u8());
  r.voice(s.stored);
  r.voice(s.recall);
  s.srcBank = bit(r.u8());
  s.srcSlot = clampU8(r.u8(), 1, 32);
  for (int i = 0; i < kFunctionCount; ++i) s.fn[i] = clampU8(r.u8(), 0, params::functionMax(64 + i));
  if (!s.crtPresent) {
    s.crtChanged = 0;
    s.crtNameLen = 0;
    std::memset(s.crtName, 0, sizeof s.crtName);
    std::memset(s.crtBank, 0, sizeof s.crtBank);
  }
}

void readRest(Reader& r, Persist& s) {
  s.selMem = bit(r.u8());
  uint8_t g[kGlobalCount];
  r.bytes(g, kGlobalCount);
  s.globals[0] = clampU8(g[0], 0, 127);  // master tune
  s.globals[1] = clampU8(g[1], 1, 16);   // MIDI channel
  s.globals[2] = bit(g[2]);              // omni
  s.globals[3] = bit(g[3]);              // SYS INFO AVAIL
  s.globals[4] = bit(g[4]);              // protect INT
  s.globals[5] = bit(g[5]);              // protect CRT
  s.globals[6] = clampU8(g[6], 0, 127);  // volume
  uint8_t t[kSettingCount];
  r.bytes(t, kSettingCount);
  for (int i = 0; i < kSettingCount; ++i) s.settings[i] = bit(t[i]);
  // Zoom: 0 = fit, else 50..200 in steps of 10.
  int z = t[5];
  if (z != 0) {
    z = z < 50 ? 50 : (z > 200 ? 200 : z);
    z = (z + 5) / 10 * 10;
    if (z > 200) z = 200;
  }
  s.settings[5] = uint8_t(z);
}

}  // namespace

size_t prefixBytes() { return kPrefix; }
size_t restBytes() { return kRest; }
size_t blobBytesV1() { return kBlobV1; }

size_t write(const Persist& s, uint8_t* dst, size_t cap) {
  if (!dst || cap < kBlobV1) return 0;
  Writer w{dst};
  w.bytes("VDX1", 4);
  w.u8(uint8_t(kVersion & 0xFF));
  w.u8(uint8_t(kVersion >> 8));
  w.u8(0);
  w.u8(0);
  // prefix
  w.bytes(s.intBank, sizeof s.intBank);
  w.u8(bit(s.crtPresent));
  w.u8(bit(s.crtChanged));
  w.u8(clampU8(s.crtNameLen, 0, kCrtNameMax));
  w.bytes(s.crtName, kCrtNameMax);
  w.bytes(s.crtBank, sizeof s.crtBank);
  w.voice(s.edit);
  w.u8(bit(s.edited));
  w.voice(s.stored);
  w.voice(s.recall);
  w.u8(s.srcBank);
  w.u8(s.srcSlot);
  w.bytes(s.fn, kFunctionCount);
  // rest
  w.u8(s.selMem);
  w.bytes(s.globals, kGlobalCount);
  w.bytes(s.settings, kSettingCount);
  const uint32_t crc = sysex::crc32(dst, w.n);
  w.u8(uint8_t(crc));
  w.u8(uint8_t(crc >> 8));
  w.u8(uint8_t(crc >> 16));
  w.u8(uint8_t(crc >> 24));
  return w.n;
}

ReadResult validate(const uint8_t* d, size_t len) {
  if (len == 0) return ReadResult::Empty;
  if (!d || len < kHeaderBytes + 4) return ReadResult::Reset;
  if (std::memcmp(d, "VDX1", 4) != 0) return ReadResult::Reset;
  if (sysex::crc32(d, len - 4) != readU32(d + len - 4)) return ReadResult::Reset;
  const unsigned version = unsigned(d[4]) | (unsigned(d[5]) << 8);
  if (version == kVersion) return len == kBlobV1 ? ReadResult::Ok : ReadResult::Reset;
  if (version > kVersion) return len >= kHeaderBytes + kPrefix + 4 ? ReadResult::Newer : ReadResult::Reset;
  return ReadResult::Reset;  // unknown older version
}

ReadResult read(const uint8_t* d, size_t len, Persist& out) {
  const ReadResult v = validate(d, len);
  if (v == ReadResult::Reset || v == ReadResult::Empty) return v;
  Reader r{d, kHeaderBytes};
  readPrefix(r, out);
  if (v == ReadResult::Ok) readRest(r, out);
  return v;
}

}  // namespace vdx::state

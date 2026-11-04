#include "sysex/voice.h"

#include <cstring>

#include "vdx/params.h"

namespace vdx::sysex {

namespace {

constexpr int kOpBlock = 21;
constexpr int kPackedOpBlock = 17;

// Carrier masks for algorithms 1..32 (bit op-1), derived from the MSFA
// FmAlgorithm table: an operator is a carrier when (flags & 7) == OUT_BUS_ADD.
constexpr uint8_t kCarriers[32] = {
    0x05, 0x05, 0x09, 0x09, 0x15, 0x15, 0x05, 0x05, 0x05, 0x09, 0x09, 0x05, 0x05, 0x05, 0x05, 0x01,
    0x01, 0x01, 0x19, 0x0B, 0x1B, 0x1D, 0x1B, 0x1F, 0x1F, 0x0B, 0x0B, 0x25, 0x17, 0x27, 0x1F, 0x3F};

}  // namespace

uint8_t carrierMask(int algorithm) {
  if (algorithm < 0) algorithm = 0;
  if (algorithm > 31) algorithm = 31;
  return kCarriers[algorithm];
}

uint8_t effectiveByte(int index, uint8_t raw) { return uint8_t(params::effective(index, raw)); }

void effectiveVoice(const uint8_t* raw155, uint8_t* eff155) {
  for (int i = 0; i < kVoiceBytes; ++i) eff155[i] = effectiveByte(i, raw155[i]);
}

void unpackVoice(const uint8_t* p, Voice& out) {
  uint8_t* u = out.raw;
  for (int i = 0; i < 6; ++i) {
    const uint8_t* po = p + i * kPackedOpBlock;
    uint8_t* uo = u + i * kOpBlock;
    for (int k = 0; k <= 10; ++k) uo[k] = po[k];
    uo[11] = uint8_t(po[11] & 3);         // LC
    uo[12] = uint8_t((po[11] >> 2) & 3);  // RC
    uo[13] = uint8_t(po[12] & 7);         // RS
    uo[20] = uint8_t((po[12] >> 3) & 15); // DET
    uo[14] = uint8_t(po[13] & 3);         // AMS
    uo[15] = uint8_t((po[13] >> 2) & 7);  // KVS
    uo[16] = po[14];                      // OL
    uo[17] = uint8_t(po[15] & 1);         // MODE
    uo[18] = uint8_t((po[15] >> 1) & 31); // COARSE
    uo[19] = po[16];                      // FINE
  }
  for (int k = 0; k < 8; ++k) u[126 + k] = p[102 + k];  // PEG rates, levels
  u[134] = p[110];                                     // algorithm
  u[135] = uint8_t(p[111] & 7);                        // FB
  u[136] = uint8_t((p[111] >> 3) & 1);                 // OKS
  for (int k = 0; k < 4; ++k) u[137 + k] = p[112 + k]; // LFO speed, delay, PMD, AMD
  u[141] = uint8_t(p[116] & 1);                        // LKS
  u[142] = uint8_t((p[116] >> 1) & 7);                 // WAVE
  u[143] = uint8_t((p[116] >> 4) & 7);                 // PMS
  u[144] = p[117];                                     // transpose
  for (int k = 0; k < kNameLen; ++k) u[145 + k] = p[118 + k];
  std::memcpy(out.packed, p, kPackedVoiceBytes);
  out.packedValid = 1;
}

void voiceFromUnpacked(const uint8_t* raw155, Voice& out) {
  std::memcpy(out.raw, raw155, kVoiceBytes);
  std::memset(out.packed, 0, kPackedVoiceBytes);
  out.packedValid = 0;
}

void packCurrent(const uint8_t* u, uint8_t* p) {
  auto e = [u](int index) { return int(effectiveByte(index, u[index])); };
  for (int i = 0; i < 6; ++i) {
    uint8_t* po = p + i * kPackedOpBlock;
    const int b = i * kOpBlock;
    for (int k = 0; k <= 10; ++k) po[k] = u[b + k];
    po[11] = uint8_t((e(b + 12) << 2) | e(b + 11));
    po[12] = uint8_t((e(b + 20) << 3) | e(b + 13));
    po[13] = uint8_t((e(b + 15) << 2) | e(b + 14));
    po[14] = u[b + 16];
    po[15] = uint8_t((e(b + 18) << 1) | e(b + 17));
    po[16] = u[b + 19];
  }
  for (int k = 0; k < 8; ++k) p[102 + k] = u[126 + k];
  p[110] = u[134];
  p[111] = uint8_t((e(136) << 3) | e(135));
  for (int k = 0; k < 4; ++k) p[112 + k] = u[137 + k];
  p[116] = uint8_t((e(143) << 4) | (e(142) << 1) | e(141));
  p[117] = u[144];
  for (int k = 0; k < kNameLen; ++k) p[118 + k] = u[145 + k];
}

void packVoice(const Voice& v, uint8_t* out128) {
  if (v.packedValid) std::memcpy(out128, v.packed, kPackedVoiceBytes);
  else packCurrent(v.raw, out128);
}

bool setByte(Voice& v, int index, uint8_t value) {
  if (index < 0 || index >= kVoiceBytes) return false;
  if (v.raw[index] == value) return false;
  v.raw[index] = value;
  v.packedValid = 0;
  return true;
}

void initOperatorRaw(uint8_t* o, int outputLevel) {
  using namespace params;
  for (int k = 0; k < 4; ++k) o[kOpR1 + k] = 99;
  o[kOpL1] = 99;
  o[kOpL2] = 99;
  o[kOpL3] = 99;
  o[kOpL4] = 0;
  o[kOpBreakPoint] = 0;
  o[kOpLeftDepth] = 0;
  o[kOpRightDepth] = 0;
  o[kOpLeftCurve] = 0;
  o[kOpRightCurve] = 0;
  o[kOpRateScaling] = 0;
  o[kOpAms] = 0;
  o[kOpKvs] = 0;
  o[kOpLevel] = uint8_t(outputLevel);
  o[kOpMode] = 0;
  o[kOpCoarse] = 1;
  o[kOpFine] = 0;
  o[kOpDetune] = 7;
}

void initVoiceRaw(uint8_t* u) {
  using namespace params;
  for (int op = 1; op <= 6; ++op) initOperatorRaw(u + opBase(op), op == 1 ? 99 : 0);
  for (int k = 0; k < 4; ++k) u[kPegR1 + k] = 99;
  for (int k = 0; k < 4; ++k) u[kPegL1 + k] = 50;
  u[kAlgorithm] = 0;
  u[kFeedback] = 0;
  u[kOscKeySync] = 1;
  u[kLfoSpeed] = 35;
  u[kLfoDelay] = 0;
  u[kLfoPmd] = 0;
  u[kLfoAmd] = 0;
  u[kLfoKeySync] = 1;
  u[kLfoWave] = 0;
  u[kPitchModSens] = 3;
  u[kTranspose] = 24;
  std::memcpy(u + kName, "INIT VOICE", kNameLen);
}

void initVoice(Voice& out) {
  initVoiceRaw(out.raw);
  packCurrent(out.raw, out.packed);
  out.packedValid = 1;
}

void displayName(const uint8_t* raw155, char* out10) {
  for (int k = 0; k < kNameLen; ++k) out10[k] = displayChar(raw155[kNameIndex + k]);
}

void packedName(const uint8_t* packed128, char* out10) {
  for (int k = 0; k < kNameLen; ++k) out10[k] = displayChar(packed128[118 + k]);
}

}  // namespace vdx::sysex

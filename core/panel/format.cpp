#include "panel/format.h"

#include <cstring>

#include "sysex/voice.h"
#include "vdx/params.h"

namespace vdx::fmt {

namespace {

// round(1000 * 10^(fine/100)), fine = 0..99 (SPEC §5.3.2 fixed value).
constexpr int16_t kFixed[100] = {
    1000, 1023, 1047, 1072, 1096, 1122, 1148, 1175, 1202, 1230, 1259, 1288, 1318, 1349, 1380, 1413, 1445,
    1479, 1514, 1549, 1585, 1622, 1660, 1698, 1738, 1778, 1820, 1862, 1905, 1950, 1995, 2042, 2089, 2138,
    2188, 2239, 2291, 2344, 2399, 2455, 2512, 2570, 2630, 2692, 2754, 2818, 2884, 2951, 3020, 3090, 3162,
    3236, 3311, 3388, 3467, 3548, 3631, 3715, 3802, 3890, 3981, 4074, 4169, 4266, 4365, 4467, 4571, 4677,
    4786, 4898, 5012, 5129, 5248, 5370, 5495, 5623, 5754, 5888, 6026, 6166, 6310, 6457, 6607, 6761, 6918,
    7079, 7244, 7413, 7586, 7762, 7943, 8128, 8318, 8511, 8710, 8913, 9120, 9333, 9550, 9772};

// MSFA lfo.cc lfoSource[speed] in Hz, rounded to hundredths.
constexpr int16_t kLfoHz100[100] = {
    6,    13,   31,   44,   62,   75,   94,   113,  125,  144,  156,  175,  188,  206,  225,  237,  256,
    269,  287,  300,  319,  337,  350,  368,  381,  400,  419,  431,  450,  462,  481,  493,  512,  532,
    543,  562,  575,  595,  606,  625,  643,  656,  675,  687,  705,  725,  738,  756,  769,  788,  799,
    818,  837,  850,  869,  881,  899,  912,  930,  950,  961,  980,  995,  1012, 1125, 1138, 1256, 1368,
    1390, 1509, 1637, 1664, 1787, 1919, 1943, 2083, 2103, 2250, 2400, 2426, 2575, 2717, 2758, 2905, 3069,
    3119, 3266, 3432, 3467, 3642, 3820, 3855, 4039, 4075, 4263, 4433, 4488, 4677, 4859, 4926};

const char* const kNotes[12] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
const char* const kWaves[6] = {"TRIANGL", "SAW DWN", "SAW UP", "SQUARE", "SINE", "S/HOLD"};
const char* const kCurves[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int put(char* out, const char* s) {
  const int n = int(std::strlen(s));
  std::memcpy(out, s, size_t(n) + 1);
  return n;
}

// Writes a non-negative integer, returns length.
int putUint(char* out, unsigned v) {
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = char('0' + v % 10);
    v /= 10;
  } while (v);
  for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  out[n] = 0;
  return n;
}

}  // namespace

int number(int v, char* out) {
  if (v < 0) {
    out[0] = '-';
    return 1 + putUint(out + 1, unsigned(-v));
  }
  return putUint(out, unsigned(v));
}

int nn(int v, char* out) {
  if (v >= 0 && v < 10) {
    out[0] = ' ';
    out[1] = char('0' + v);
    out[2] = 0;
    return 2;
  }
  return number(v, out);
}

int noteName(int midi, char* out) {
  if (midi < 0) midi = 0;
  out[0] = kNotes[midi % 12][0];
  out[1] = kNotes[midi % 12][1];
  return 2 + number(midi / 12 - 2, out + 2);
}

int ratio(int coarse, int fine, char* out) {
  coarse = clampi(coarse, 0, 31);
  fine = clampi(fine, 0, 99);
  const int r = coarse >= 1 ? coarse * (100 + fine) : 50 + fine / 2;
  const int whole = r / 100, frac = r % 100;
  out[0] = whole >= 10 ? char('0' + whole / 10) : ' ';
  out[1] = char('0' + whole % 10);
  out[2] = '.';
  out[3] = char('0' + frac / 10);
  out[4] = char('0' + frac % 10);
  out[5] = 0;
  return 5;
}

int fixedFreq(int coarse, int fine, char* out) {
  const int f = kFixed[clampi(fine, 0, 99)];
  const int point = (coarse & 3) + 1;  // digits before the point
  const char d[4] = {char('0' + f / 1000), char('0' + (f / 100) % 10), char('0' + (f / 10) % 10),
                     char('0' + f % 10)};
  int n = 0;
  for (int i = 0; i < 4; ++i) {
    out[n++] = d[i];
    if (i + 1 == point && point < 4) out[n++] = '.';
  }
  if (point == 4) out[n++] = ' ';
  out[n++] = 'H';
  out[n++] = 'z';
  out[n] = 0;
  return n;
}

int opFrequency(int mode, int coarse, int fine, char* out) {
  return mode ? fixedFreq(coarse, fine, out) : ratio(coarse, fine, out);
}

int detune(int v, char* out) {
  const int d = clampi(v, 0, 14) - 7;
  out[0] = d < 0 ? '-' : (d > 0 ? '+' : ' ');
  out[1] = char('0' + (d < 0 ? -d : d));
  out[2] = 0;
  return 2;
}

const char* wave(int w) { return kWaves[clampi(w, 0, 5)]; }
const char* curve(int c) { return kCurves[clampi(c, 0, 3)]; }
const char* onOff(int v) { return v ? "ON" : "OFF"; }
const char* oscMode(int mode) { return mode ? "FIXED FREQ.(Hz)" : "FREQUENCY(RATIO)"; }

int masterTune(int v, char* out) {
  const int n = (clampi(v, 0, 127) - 64) * 750;  // tenths of a cent x 64
  const int tenths = n >= 0 ? (n + 32) / 64 : -((-n + 32) / 64);
  int k = 0;
  if (tenths > 0) out[k++] = '+';
  if (tenths < 0) out[k++] = '-';
  const int a = tenths < 0 ? -tenths : tenths;
  k += putUint(out + k, unsigned(a / 10));
  out[k++] = '.';
  out[k++] = char('0' + a % 10);
  return k + put(out + k, " cents");
}

int lfoHz(int speed, char* out) {
  const int h = kLfoHz100[clampi(speed, 0, 99)];
  int k = putUint(out, unsigned(h / 100));
  out[k++] = '.';
  out[k++] = char('0' + (h / 10) % 10);
  out[k++] = char('0' + h % 10);
  return k + put(out + k, " Hz");
}

int trim(const char* in, char* out) {
  const char* b = in;
  while (*b == ' ') ++b;
  int n = int(std::strlen(b));
  while (n > 0 && b[n - 1] == ' ') --n;
  std::memmove(out, b, size_t(n));
  out[n] = 0;
  return n;
}

int maskDigits(int mask, char* out) {
  for (int op = 1; op <= 6; ++op) out[op - 1] = (mask >> (6 - op)) & 1 ? '1' : '0';
  out[6] = 0;
  return 6;
}

int voiceField(const uint8_t* eff, int index, int value, char* out) {
  using namespace params;
  if (index < 0 || index > kOpMask) return put(out, "");
  if (index == kOpMask) return maskDigits(value, out);
  if (index >= kName) {
    out[0] = sysex::displayChar(uint8_t(value));
    out[1] = 0;
    return 1;
  }
  const int v = effective(index, value);
  if (index < 126) {
    const int base = index - index % kOpBytes;
    switch (index % kOpBytes) {
      case kOpBreakPoint: return noteName(21 + v, out);
      case kOpLeftCurve:
      case kOpRightCurve: return put(out, curve(v));
      case kOpMode: return put(out, oscMode(v));
      case kOpCoarse: return opFrequency(eff[base + kOpMode], v, eff[base + kOpFine], out);
      case kOpFine: return opFrequency(eff[base + kOpMode], eff[base + kOpCoarse], v, out);
      case kOpDetune: return detune(v, out);
      default: return nn(v, out);
    }
  }
  switch (index) {
    case kAlgorithm: return nn(v + 1, out);
    case kOscKeySync:
    case kLfoKeySync: return put(out, onOff(v));
    case kLfoWave: return put(out, wave(v));
    case kTranspose: return noteName(36 + v, out);
    default: return nn(v, out);
  }
}

int voiceString(const uint8_t* eff, int index, int value, char* out) {
  char tmp[kMaxText];
  voiceField(eff, index, value, tmp);
  if (index >= params::kName && index < params::kName + 10) return put(out, tmp);  // a name char may be a space
  return trim(tmp, out);
}

int assignBitString(int assignValue, int bit, char* out) { return put(out, onOff((assignValue >> bit) & 1)); }

int functionField(const uint8_t* fn, int n, int value, char* out) {
  using namespace params;
  if (n < kFnFirst || n > kFnLast) return put(out, "");
  const int v = clampi(value, 0, functionMax(n));
  switch (n) {
    case kFnMono: return put(out, v ? "MONO MODE" : "POLY MODE");
    case kFnBendRange: return nn(fn[kFnBendStep - kFnFirst] > 0 ? 12 : v, out);
    case kFnPortaMode:
      if (fn[kFnMono - kFnFirst]) return put(out, v ? "FULL TIME PORTA" : "FINGERED PORTA");
      return put(out, v ? "SUS-KEY P FOLLOW" : "SUS-KEY P RETAIN");
    case kFnGlissando: return put(out, onOff(v));
    case kFnWheelAssign:
    case kFnFootAssign:
    case kFnBreathAssign:
    case kFnAtAssign: {
      int k = put(out, onOff(v & 1));
      out[k++] = ' ';
      k += put(out + k, onOff((v >> 1) & 1));
      out[k++] = ' ';
      return k + put(out + k, onOff((v >> 2) & 1));
    }
    default: return nn(v, out);
  }
}

int functionString(const uint8_t* fn, int n, int value, char* out) {
  uint8_t tmp[14];
  std::memcpy(tmp, fn, 14);
  if (n >= params::kFnFirst && n <= params::kFnLast) tmp[n - params::kFnFirst] = uint8_t(value);
  char field[kMaxText];
  functionField(tmp, n, value, field);
  return trim(field, out);
}

int globalString(int global, int value, char* out) {
  switch (global) {
    case 0: return masterTune(value, out);                     // MasterTune
    case 1: return number(clampi(value, 1, 16), out);           // MidiChannel
    case 2: return put(out, onOff(value));                      // Omni
    case 3: return put(out, value ? "AVAIL" : "UNAVAIL");       // SysInfo
    case 4:
    case 5: return put(out, onOff(value));                      // ProtectInt / ProtectCrt
    case 6: return number(clampi(value, 0, 127), out);          // Volume
    default: return put(out, "");
  }
}

}  // namespace vdx::fmt

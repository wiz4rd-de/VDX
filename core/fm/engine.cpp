/*
 * Portions ported from MSFA as distributed with Dexed (source/msfa):
 *   Copyright 2012, 2013 Google Inc.; Dexed contributors.
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *        http://www.apache.org/licenses/LICENSE-2.0
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 * Modified for VDX: Mk I kernel, chain feedback, DAC stage, deterministic math, voice allocation and portamento per SPEC R-E3..R-E12.
 */
// VDX sound engine (SPEC §7.1, R-E1..R-E14).
#include "fm/engine.h"

#include <climits>
#include <cmath>
#include <cstring>

#include "fm/msfa_exp2.h"
#include "fm/detmath.h"
#include "fm/dx7tables.h"
#include "fm/engine_impl.h"
#include "fm_core.h"
#include "fm_op_kernel.h"
#include "fm/msfa_sin.h"

namespace vdx::fm {

const uint16_t kLogsin[1024] = {
#include "fm/tables/logsin.inc"
};
const uint16_t kExptab[1024] = {
#include "fm/tables/exptab.inc"
};
const int32_t kFreqlut49096[1025] = {
#include "fm/tables/freqlut_49096.inc"
};
const int32_t kFinetab[100] = {
#include "fm/tables/finetab.inc"
};

namespace {

struct AlgTable : FmCore {
  static int flags(int a, int i) { return algorithms[a].ops[i]; }
};

struct MsfaTables {
  MsfaTables() {
    Sin::init();
    Exp2::init();
  }
};

void ensureTables() { static MsfaTables once; }

// round(1024 * log2(C)) for C = 0..6 (R-E3 carrierAtt).
constexpr int kCarrierAtt[7] = {0, 0, 1024, 1623, 2048, 2378, 2647};
// round(65536 / C) (R-E14 Modern linear carrier scaling).
constexpr int kCarrierRecip[7] = {65536, 65536, 32768, 21845, 16384, 13107, 10923};

constexpr int32_t kSemitone = (1 << 24) / 12;  // 1398101
constexpr int kSilentLevel = 512;              // L <= 512 -> Mk I output is exactly 0
constexpr int32_t kLevelThresh = 1120;         // MSFA FmCore gain threshold

inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int32_t iabs32(int32_t a) { return a < 0 ? -a : a; }

struct ChainInfo {
  int start = -1;  // index of the FB_IN op
  int end = -1;    // index of the FB_OUT op (== start for self feedback)
};

ChainInfo chainOf(int alg) {
  ChainInfo c;
  for (int i = 0; i < 6; ++i)
    if (AlgTable::flags(alg, i) & kFbIn) c.start = i;
  if (c.start < 0) return c;
  c.end = c.start;
  for (int i = 0; i < 6; ++i)
    if (i != c.start && (AlgTable::flags(alg, i) & kFbOut)) c.end = i;
  return c;
}

}  // namespace

// ---- free functions (R-E3, R-E5, R-E6, R-E8, R-E9) -----------------------

int algorithmFlags(int algorithm, int index) { return AlgTable::flags(algorithm, index); }

int carrierCount(int algorithm) {
  int n = 0;
  for (int i = 0; i < 6; ++i)
    if ((AlgTable::flags(algorithm, i) & 7) == kOutBusAdd) ++n;
  return n;
}

int32_t noteLogfreq(int note) {
  const int32_t base = 50857777;  // (1 << 24) * (log2(440) - 69/12)
  return base + kSemitone * note;
}

int32_t oscFreq(int note, int mode, int coarse, int fine, int detune) {
  int32_t logfreq;
  if (mode == 0) {
    logfreq = noteLogfreq(note);
    // Dexed dx7note.cc (Apache): detune scaled by pitch.
    const double detuneRatio = 0.0209 * det::exp(-0.396 * (double(logfreq) / double(1 << 24))) / 7.0;
    logfreq = int32_t(double(logfreq) + detuneRatio * double(logfreq) * double(detune - 7));
    logfreq += tab::coarsemul[coarse & 31];
    logfreq += kFinetab[fine];
  } else {
    logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
    logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
  }
  return logfreq;
}

int scaleVelocity(int velocity, int sensitivity) {
  const int v = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
  // Reference velocity_data[50] (velocity 100) so that velocity 100 is exactly
  // 0 dB at every KVS (AC-E5.2; see DECISIONS.md).
  const int value = int(tab::velocity_data[v >> 1]) - int(tab::velocity_data[50]);
  return ((sensitivity * value + 7) >> 3) * 16;
}

int scaleRate(int note, int sensitivity) {
  int x = note / 3 - 7;
  x = x < 0 ? 0 : (x > 31 ? 31 : x);
  return (sensitivity * x) >> 3;
}

static int scaleCurve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    scale = (group * depth * 329) >> 12;
  } else {
    const int raw = tab::exp_scale_data[imin(group, 32)];
    scale = (raw * depth * 329) >> 15;
  }
  return curve < 2 ? -scale : scale;
}

int scaleLevel(int note, int bp, int ld, int rd, int lc, int rc) {
  const int offset = note - bp - 17;
  if (offset >= 0) return scaleCurve((offset + 1) / 3, rd, rc);
  return scaleCurve(-(offset - 1) / 3, ld, lc);
}

int32_t dacQuantise(int32_t s) {
  s = s < -16384 ? -16384 : (s > 16383 ? 16383 : s);
  const int32_t m = s < 0 ? -s : s;
  const int e = m < 2048 ? 0 : m < 4096 ? 1 : m < 8192 ? 2 : 3;
  const int32_t q = (m >> e) << e;
  return s < 0 ? -q : q;
}

int32_t mk1Kernel(uint32_t p, int L, int carrierAtt) {
  p &= 0xFFFFFFu;
  const uint32_t q = p >> 22;
  uint32_t i = (p >> 12) & 1023u;
  if (q & 1u) i = 1023u - i;
  if (L > 4095) L = 4095;
  if (L < 0) L = 0;
  const int att = int(kLogsin[i]) + ((4096 - L) << 2) + carrierAtt;
  const int sh = att >> 10;
  const int32_t y14 = sh >= 14 ? 0 : (int32_t(kExptab[att & 1023]) >> sh);
  return (q >= 2 ? -y14 : y14) * 8192;
}

// ---- Impl --------------------------------------------------------------

Engine::Impl::Impl() {
  ensureTables();
  // INIT VOICE defaults until the core sets a voice.
  for (int op = 0; op < 6; ++op) {
    uint8_t* o = voice + op * 21;
    o[0] = o[1] = o[2] = o[3] = 99;
    o[4] = o[5] = o[6] = 99;
    o[7] = 0;
    o[18] = 1;
    o[20] = 7;
    o[16] = op == 5 ? 99 : 0;
  }
  for (int i = 126; i < 130; ++i) voice[i] = 99;
  for (int i = 130; i < 134; ++i) voice[i] = 50;
  voice[136] = 1;
  voice[137] = 35;
  voice[141] = 1;
  voice[143] = 3;
  voice[144] = 24;
  voice[155] = 0x3F;
  prepare(kInternalRate);
}

void Engine::Impl::prepare(double r) {
  rate = r;
  internalRate = std::fabs(r - kInternalRate) < 1e-9;
  if (internalRate) {
    std::memcpy(freqlut, kFreqlut49096, sizeof(freqlut));
  } else {
    const double base = double(1ll << 44) / r;
    for (int i = 0; i < 1025; ++i) freqlut[i] = int32_t(std::floor(base * det::exp2(double(i) / 1024.0) + 0.5));
  }
  lfo.init(r);
  pegUnit = PitchEnv::unitFor(r);
  for (int t = 0; t < 100; ++t) {
    const double semisPerSec = 2100.0 * det::exp2(-0.062 * double(t));
    portaRate[t] = int32_t(std::floor(semisPerSec * double(kN) / r * (double(1 << 24) / 12.0) + 0.5));
  }
  egRatioQ32 = uint64_t(std::llround(kInternalRate / r * 4294967296.0));
  egAcc = 0;
  // 2-pole Butterworth low-pass at 16 kHz, TPT state-variable form (R-E9).
  const double fc = 16000.0 < 0.45 * r ? 16000.0 : 0.45 * r;
  const double g = std::tan(3.14159265358979323846 * fc / r);
  const double k = 1.4142135623730950488;
  svfA1 = 1.0 / (1.0 + g * (g + k));
  svfA2 = g * svfA1;
  svfA3 = g * svfA2;
  ic1 = ic2 = 0.0;
  for (auto& v : voices) {
    v.active = v.keyDown = v.sustained = false;
    for (auto& o : v.op) o.env.silence();
  }
  monoCount = 0;
  lfoKey[0] = -1;
  voiceDirty = true;
}

int Engine::Impl::foldNote(int note) const {
  int n = note + int(voice[144]) - 24;
  while (n > 127) n -= 12;
  while (n < 0) n += 12;
  return n;
}

bool Engine::Impl::anyKeyHeld() const {
  for (const auto& v : voices)
    if (v.active && v.keyDown) return true;
  return false;
}

bool Engine::Impl::portaActive() const { return fn.portaTime > 0 && portaSwitch; }

int32_t Engine::Impl::currentPitch(const VoiceRt& v) const { return noteLogfreq(v.playNote) + v.porta; }

void Engine::Impl::computeOps(VoiceRt& v, bool init) {
  for (int idx = 0; idx < 6; ++idx) {
    const uint8_t* o = voice + idx * 21;
    int rates[4], levels[4];
    for (int i = 0; i < 4; ++i) {
      rates[i] = o[i];
      levels[i] = o[4 + i];
    }
    int outlevel = scaleoutlevel(o[16]);
    outlevel += scaleLevel(v.playNote, o[8], o[9], o[10], o[11], o[12]);
    outlevel = imin(127, outlevel);
    outlevel = outlevel * 32 + scaleVelocity(v.velocity, o[15]);
    const int rs = scaleRate(v.playNote, o[13]);
    OpRt& op = v.op[idx];
    if (init) op.env.init(rates, levels, outlevel, rs);
    else op.env.update(rates, levels, outlevel, rs);
    op.fixed = o[17] != 0;
    op.base = oscFreq(v.playNote, o[17], o[18], o[19], o[20]);
    op.ams = tab::ampmodsenstab[o[14] & 3];
  }
  int r[4], l[4];
  for (int i = 0; i < 4; ++i) {
    r[i] = voice[126 + i];
    l[i] = voice[130 + i];
  }
  v.peg.setUnit(pegUnit);
  if (init) v.peg.set(r, l);
  else v.peg.update(r, l);
}

void Engine::Impl::setNotePitch(VoiceRt& v, int note) {
  v.note = note;
  v.playNote = foldNote(note);
  for (int idx = 0; idx < 6; ++idx) {
    const uint8_t* o = voice + idx * 21;
    v.op[idx].base = oscFreq(v.playNote, o[17], o[18], o[19], o[20]);
  }
}

void Engine::Impl::applyVoiceData() {
  voiceDirty = false;
  alg = voice[134] & 31;
  const int feedback = voice[135];
  fbShift = feedback != 0 ? 8 - feedback : 16;
  pmd = uint32_t((voice[139] * 165) >> 6);
  amd = uint32_t((voice[140] * 165) >> 6);
  pms = tab::pitchmodsenstab[voice[143] & 7];
  if (lfoKey[0] != voice[137] || lfoKey[1] != voice[138] || lfoKey[2] != voice[141] || lfoKey[3] != voice[142]) {
    lfoKey[0] = voice[137];
    lfoKey[1] = voice[138];
    lfoKey[2] = voice[141];
    lfoKey[3] = voice[142];
    lfo.reset(voice[137], voice[138], voice[141], voice[142]);
  }
  for (auto& v : voices) {
    if (!v.active) continue;
    v.playNote = foldNote(v.note);
    computeOps(v, false);
  }
}

int Engine::Impl::pickVoice(int note) const {
  int best = 0, bestScore = -1;
  uint64_t bestSerial = ~0ull;
  for (int i = 0; i < kVoices; ++i) {
    const VoiceRt& v = voices[i];
    int score = 0;
    if (!v.active) score += 4;
    else if (!v.keyDown) score += 2;
    if (v.active && v.note == note) score += 1;
    if (score > bestScore || (score == bestScore && v.serial < bestSerial)) {
      best = i;
      bestScore = score;
      bestSerial = v.serial;
    }
  }
  return best;
}

void Engine::Impl::startVoice(VoiceRt& v, int note, int velocity, int32_t portaFrom, bool) {
  if (voiceDirty) applyVoiceData();
  v.note = note;
  v.playNote = foldNote(note);
  v.velocity = velocity;
  v.serial = ++serialCounter;
  v.keyDown = true;
  v.sustained = false;
  v.active = true;
  computeOps(v, true);
  if (voice[136]) {  // oscillator key sync
    for (auto& o : v.op) o.phase = 0;
    v.fb[0] = v.fb[1] = 0;
  }
  v.porta = portaFrom != INT32_MIN ? portaFrom - noteLogfreq(v.playNote) : 0;
}

void Engine::Impl::releaseVoice(VoiceRt& v) {
  v.keyDown = false;
  if (sustainOn) {
    v.sustained = true;
    return;
  }
  v.sustained = false;
  for (auto& o : v.op) o.env.keyOff();
  v.peg.keydown(false);
}

void Engine::Impl::noteOn(int note, int velocity) {
  if (classicVelocity) velocity = (velocity * 100 + 63) / 127;
  if (velocity < 1) velocity = 1;
  const bool held = anyKeyHeld();
  if (fn.mono) {
    // note stack, last-note priority
    int w = 0;
    for (int i = 0; i < monoCount; ++i)
      if (monoStack[i] != note) monoStack[w++] = monoStack[i];
    monoCount = w;
    if (monoCount < 128) monoStack[monoCount++] = note;
    VoiceRt& v = voices[0];
    const bool legato = held && v.active && v.keyDown;
    if (legato) {
      const int32_t cur = currentPitch(v);
      setNotePitch(v, note);
      v.porta = portaActive() ? cur - noteLogfreq(v.playNote) : 0;
    } else {
      int32_t from = INT32_MIN;
      if (portaActive() && fn.portaMode == 1 && lastPlayNote >= 0)
        from = v.active ? currentPitch(v) : noteLogfreq(lastPlayNote);
      startVoice(v, note, velocity, from, true);
      lfo.keydown();
    }
    lastPlayNote = v.playNote;
    return;
  }
  const int idx = pickVoice(note);
  VoiceRt& v = voices[idx];
  int32_t from = INT32_MIN;
  if (portaActive() && lastPlayNote >= 0) from = noteLogfreq(lastPlayNote);
  startVoice(v, note, velocity, from, true);
  if (fn.portaMode == 1 && portaActive()) {
    for (auto& w : voices) {
      if (&w == &v || !w.active || w.keyDown || !w.sustained) continue;
      const int32_t cur = currentPitch(w);
      setNotePitch(w, note);
      w.porta = cur - noteLogfreq(w.playNote);
    }
  }
  if (!held) lfo.keydown();
  lastPlayNote = v.playNote;
}

void Engine::Impl::noteOff(int note) {
  if (fn.mono) {
    int w = 0;
    for (int i = 0; i < monoCount; ++i)
      if (monoStack[i] != note) monoStack[w++] = monoStack[i];
    monoCount = w;
    VoiceRt& v = voices[0];
    if (!v.active || !v.keyDown || v.note != note) return;
    if (monoCount > 0) {
      const int32_t cur = currentPitch(v);
      setNotePitch(v, monoStack[monoCount - 1]);
      v.porta = portaActive() ? cur - noteLogfreq(v.playNote) : 0;
      lastPlayNote = v.playNote;
    } else {
      releaseVoice(v);
    }
    return;
  }
  VoiceRt* oldest = nullptr;
  for (auto& v : voices)
    if (v.active && v.keyDown && v.note == note && (!oldest || v.serial < oldest->serial)) oldest = &v;
  if (oldest) releaseVoice(*oldest);
}

void Engine::Impl::refreshControllers() {
  ampMod = pitchMod = egMod = 0;
  auto apply = [this](int cc, int range, int assign) {
    const int total = cc * range / 100;
    if (assign & 2) ampMod = imax(ampMod, total);
    if (assign & 1) pitchMod = imax(pitchMod, total);
    if (assign & 4) egMod = imax(egMod, total);
  };
  apply(modwheel, fn.wheelRange, fn.wheelAssign);
  apply(breathCc, fn.breathRange, fn.breathAssign);
  apply(footCc, fn.footRange, fn.footAssign);
  apply(atCc, fn.atRange, fn.atAssign);
  if (!((fn.wheelAssign | fn.footAssign | fn.breathAssign | fn.atAssign) & 4)) egMod = 127;
}

namespace {

inline uint32_t lookupInc(const int32_t* lut, int32_t lf) {
  const int ix = (lf & 0xffffff) >> 14;
  const int32_t y0 = lut[ix], y1 = lut[ix + 1];
  const int low = lf & 0x3fff;
  const int32_t y = y0 + int32_t((int64_t(y1 - y0) * low) >> 14);
  const int hi = lf >> 24;
  return uint32_t(y >> (20 - hi));
}

inline void routeOut(int32_t* out, const int32_t* y, bool add) {
  if (add)
    for (int i = 0; i < kBlock; ++i) out[i] += y[i];
  else
    std::memcpy(out, y, sizeof(int32_t) * kBlock);
}

inline int32_t peakOf(const int32_t* y) {
  int32_t p = 0;
  for (int i = 0; i < kBlock; ++i) {
    const int32_t a = iabs32(y[i]);
    p = a > p ? a : p;
  }
  return p;
}

}  // namespace

void Engine::Impl::renderVoice(VoiceRt& v, int32_t* vsum) {
  // ---- pitch modulation (per block; MSFA dx7note.cc compute()) ----
  const uint32_t pmdQ = pmd * uint32_t(blkLfoDelay);
  const int32_t senslfo = pms * (blkLfoVal - (1 << 23));
  int32_t pmod1 = iabs32(int32_t((int64_t(pmdQ) * int64_t(senslfo)) >> 39));
  int32_t pmod2 = iabs32(int32_t((int64_t(pitchMod) * int64_t(senslfo)) >> 14));
  int32_t pitch = pmod1 > pmod2 ? pmod1 : pmod2;
  if (senslfo < 0) pitch = -pitch;
  pitch += v.peg.getsample();
  pitch += blkPitchBase;
  v.lastPitchMod = pitch;

  // ---- amplitude modulation ----
  const int32_t lfoAm = (1 << 24) - blkLfoVal;
  uint32_t amod1 = uint32_t((int64_t(amd) * int64_t(blkLfoDelay)) >> 8);
  amod1 = uint32_t((int64_t(amod1) * int64_t(lfoAm)) >> 24);
  const uint32_t amod2 = uint32_t((int64_t(ampMod) * int64_t(lfoAm)) >> 7);
  uint32_t amdMod = amod1 > amod2 ? amod1 : amod2;
  const uint32_t egBias = (1u << 24) - (uint32_t(egMod + 1) << 17);
  if (egBias > amdMod) amdMod = egBias;

  // ---- portamento (R-E12) ----
  if (v.porta != 0) {
    if (!portaActive()) {
      v.porta = 0;
    } else {
      const int32_t r = portaRate[fn.portaTime];
      if (v.porta > 0) v.porta = v.porta > r ? v.porta - r : 0;
      else v.porta = -v.porta > r ? v.porta + r : 0;
    }
  }
  int32_t porta = v.porta;
  if (fn.gliss && porta != 0) {
    const int64_t num = int64_t(porta) * 12;
    const int64_t k = num >= 0 ? (num + (1 << 23)) >> 24 : -((-num + (1 << 23)) >> 24);
    porta = int32_t(k * (int64_t(1) << 24) / 12);
  }

  const bool mk1 = mode == EngineMode::MkI;
  const bool quant = mk1 && !dbg.noQuantise;
  const uint8_t mask = voice[155];
  uint32_t inc[6];
  int maxL[6];
  bool on[6];
  for (int idx = 0; idx < 6; ++idx) {
    OpRt& op = v.op[idx];
    int32_t lf = op.base + (op.fixed ? blkPitchBase : pitch + porta);
    if (quant) lf = (lf + (1 << 13)) & ~int32_t((1 << 14) - 1);  // R-E6 Mk I quantisation
    if (lf < 0) lf = 0;
    if (lf > (20 << 24) - (1 << 14)) lf = (20 << 24) - (1 << 14);
    op.lastLogfreq = lf;
    if (dbg.logfreqLog && dbg.logfreqCount < dbg.logfreqCap) dbg.logfreqLog[dbg.logfreqCount++] = lf;
    inc[idx] = lookupInc(freqlut, lf);
    on[idx] = ((mask >> idx) & 1) != 0;

    // envelope, one state update per EG clock (R-E4)
    Env& e = op.env;
    int32_t* Lb = L[idx];
    if (internalRate) {
      const uint32_t c0 = clockStart[0];
      for (int i = 0; i < kBlock; ++i) {
        e.clock(c0 + uint32_t(i));
        Lb[i] = e.level();
      }
    } else {
      for (int i = 0; i < kBlock; ++i) {
        for (int k = 0; k < clocks[i]; ++k) e.clock(clockStart[i] + uint32_t(k));
        Lb[i] = e.level();
      }
    }
    if (op.ams != 0) {
      const uint32_t sensamp = uint32_t((uint64_t(amdMod) * uint64_t(op.ams)) >> 24);
      const uint32_t pt = uint32_t(det::exp(double(sensamp) / 262144.0 * 0.07 + 12.2));
      for (int i = 0; i < kBlock; ++i) {
        const int32_t l = Lb[i] - int32_t((int64_t(Lb[i]) * int64_t(pt)) >> 24);
        Lb[i] = l < 0 ? 0 : l;
      }
    }
    int m = 0;
    for (int i = 0; i < kBlock; ++i) m = Lb[i] > m ? Lb[i] : m;
    maxL[idx] = m;
  }

  // ---- operators (R-E3 / R-E8 / R-E14) ----
  std::memset(vsum, 0, sizeof(int32_t) * kBlock);
  int32_t bus[2][kBlock];
  bool has[3] = {true, false, false};
  const int C = carrierCount(alg);
  const int cAtt = kCarrierAtt[C];
  ChainInfo ch;
  if (fbShift < 16) ch = chainOf(alg);
  const bool chain = ch.start >= 0 && ch.end != ch.start;
  int32_t y[kBlock];

  // Modern: carrier 1/C as a linear gain on the carrier itself, so feedback
  // and loudness match Mk I's in-kernel carrierAtt (R-E14).
  const int64_t recip = kCarrierRecip[C];
  auto gainFor = [&](int idx) -> int32_t {
    if (!on[idx]) return 0;
    const int32_t g = Exp2::lookup((L[idx][kBlock - 1] << 16) - (14 << 24));
    if ((AlgTable::flags(alg, idx) & 3) == 0) return int32_t((int64_t(g) * recip) >> 16);
    return g;
  };

  for (int idx = 0; idx < 6; ++idx) {
    const int flags = AlgTable::flags(alg, idx);
    const int outbus = flags & 3;
    const int inbus = (flags >> 4) & 3;
    OpRt& op = v.op[idx];

    if (chain && idx >= ch.start && idx <= ch.end) {
      if (idx != ch.start) continue;
      // Feedback across a chain (algorithms 4 and 6): per-sample interleave.
      int32_t chOut[6][kBlock];
      int32_t gain[6] = {}, dgain[6] = {};
      uint32_t ph[6];
      for (int j = ch.start; j <= ch.end; ++j) {
        const int32_t g1 = v.op[j].gainOut, g2 = gainFor(j);
        v.op[j].gainOut = g2;
        gain[j] = g1;
        dgain[j] = (g2 - g1 + (kBlock >> 1)) >> 6;
        ph[j] = v.op[j].phase;
      }
      for (int i = 0; i < kBlock; ++i) {
        int32_t x = 0;
        for (int j = ch.start; j <= ch.end; ++j) {
          const int32_t modIn = j == ch.start ? ((v.fb[0] + v.fb[1]) >> (fbShift + 1)) : x;
          int32_t out;
          if (mk1) {
            const bool carrier = (AlgTable::flags(alg, j) & 3) == 0;
            out = on[j] ? mk1Kernel(ph[j] + uint32_t(modIn), L[j][i], carrier ? cAtt : 0) : 0;
          } else {
            gain[j] += dgain[j];
            out = on[j] ? int32_t((int64_t(Sin::lookup(int32_t(ph[j]) + modIn)) * int64_t(gain[j])) >> 24) : 0;
          }
          ph[j] += inc[j];
          x = out;
          chOut[j][i] = out;
          if (j == ch.end) {
            v.fb[0] = v.fb[1];
            v.fb[1] = out;
          }
        }
      }
      for (int j = ch.start; j <= ch.end; ++j) {
        v.op[j].phase = ph[j];
        const int f = AlgTable::flags(alg, j);
        const int ob = f & 3;
        bool add = (f & kOutBusAdd) != 0;
        if (!has[ob]) add = false;
        routeOut(ob == 0 ? vsum : bus[ob - 1], chOut[j], add);
        has[ob] = true;
        const int32_t pk = peakOf(chOut[j]);
        if (pk > peaks[j]) peaks[j] = pk;
      }
      continue;
    }

    int32_t* out = outbus == 0 ? vsum : bus[outbus - 1];
    bool add = (flags & kOutBusAdd) != 0;
    const int32_t g1 = op.gainOut, g2 = gainFor(idx);
    op.gainOut = g2;
    const bool silent = mk1 ? (!on[idx] || maxL[idx] <= kSilentLevel)
                            : (!on[idx] || !(g1 >= kLevelThresh || g2 >= kLevelThresh));
    if (silent) {
      if (!add) has[outbus] = false;
      op.phase += inc[idx] << 6;
      continue;
    }
    if (!has[outbus]) add = false;
    const int32_t* in = (inbus == 0 || !has[inbus]) ? nullptr : bus[inbus - 1];
    const bool fbop = in == nullptr && (flags & 0xc0) == 0xc0 && fbShift < 16;
    if (mk1) {
      const int ca = outbus == 0 ? cAtt : 0;
      const int32_t* Lb = L[idx];
      const uint32_t p0 = op.phase, f = inc[idx];
      if (fbop) {
        int32_t y0 = v.fb[0], y1 = v.fb[1];
        for (int i = 0; i < kBlock; ++i) {
          const int32_t s = (y0 + y1) >> (fbShift + 1);
          y0 = y1;
          y1 = mk1Kernel(p0 + uint32_t(i) * f + uint32_t(s), Lb[i], ca);
          y[i] = y1;
        }
        v.fb[0] = y0;
        v.fb[1] = y1;
      } else if (in) {
        for (int i = 0; i < kBlock; ++i) y[i] = mk1Kernel(p0 + uint32_t(i) * f + uint32_t(in[i]), Lb[i], ca);
      } else {
        for (int i = 0; i < kBlock; ++i) y[i] = mk1Kernel(p0 + uint32_t(i) * f, Lb[i], ca);
      }
    } else {
      const int32_t p0 = int32_t(op.phase), f = int32_t(inc[idx]);
      if (fbop) FmOpKernel::compute_fb(y, p0, f, g1, g2, v.fb, fbShift, false);
      else if (in) FmOpKernel::compute(y, in, p0, f, g1, g2, false);
      else FmOpKernel::compute_pure(y, p0, f, g1, g2, false);
    }
    op.phase += inc[idx] << 6;
    routeOut(out, y, add);
    has[outbus] = true;
    const int32_t pk = peakOf(y);
    if (pk > peaks[idx]) peaks[idx] = pk;
  }

}

void Engine::Impl::render(int32_t* tap, float* out) {
  if (voiceDirty) applyVoiceData();
  if (internalRate) {
    for (int i = 0; i < kBlock; ++i) {
      clocks[i] = 1;
      clockStart[i] = egClock + uint32_t(i);
    }
    egClock += kBlock;
  } else {
    for (int i = 0; i < kBlock; ++i) {
      egAcc += egRatioQ32;
      const uint32_t n = uint32_t(egAcc >> 32);
      egAcc &= 0xFFFFFFFFull;
      clocks[i] = uint8_t(n);
      clockStart[i] = egClock;
      egClock += n;
    }
  }
  blkLfoVal = lfo.getsample();
  blkLfoDelay = lfo.getdelay();

  int32_t bendLf = 0;
  const int pb = bend - 8192;
  if (pb != 0) {
    if (fn.bendStep == 0) {
      bendLf = int32_t((int64_t(pb) * fn.bendRange * 2048) / 12);
    } else {
      const int64_t num = int64_t(pb) * 12, den = int64_t(8192) * fn.bendStep;
      const int64_t k = num >= 0 ? (2 * num + den) / (2 * den) : -((-2 * num + den) / (2 * den));
      bendLf = int32_t(k * fn.bendStep * (int64_t(1) << 24) / 12);
    }
  }
  blkPitchBase = bendLf + (masterTune - 64) * 16384;  // 75/64 cents per step = 2^14 (R-E6)

  const bool dacOn = mode == EngineMode::MkI && dac && !dbg.forceNoDac;
  std::memset(tap, 0, sizeof(int32_t) * kBlock);
  int64_t acc[kBlock] = {};
  int32_t vsum[kBlock];
  for (auto& v : voices) {
    if (!v.active) continue;
    renderVoice(v, vsum);
    if (dacOn) {
      for (int i = 0; i < kBlock; ++i) tap[i] += dacQuantise(vsum[i] >> 11);
    } else {
      for (int i = 0; i < kBlock; ++i) {
        tap[i] += vsum[i] >> 11;
        acc[i] += vsum[i];
      }
    }
    // A released voice whose carriers have decayed to silence becomes idle.
    if (!v.keyDown && !v.sustained) {
      bool silent = true;
      for (int idx = 0; idx < 6 && silent; ++idx) {
        if ((AlgTable::flags(alg, idx) & 3) != 0) continue;
        const Env& e = v.op[idx].env;
        const bool settled = e.stage() >= 4 || (e.stage() == 3 && !e.down() && e.target() <= kSilentLevel);
        if (!(settled && e.level() <= kSilentLevel)) silent = false;
      }
      if (silent) v.active = false;
    }
  }
  if (dacOn) {
    const double dc = (blockCount & 1) ? -1e-20 : 1e-20;  // R-E15
    for (int i = 0; i < kBlock; ++i) {
      const double x = double(tap[i]);
      const double v3 = x - ic2;
      const double v1 = svfA1 * ic1 + svfA2 * v3;
      const double v2 = ic2 + svfA2 * ic1 + svfA3 * v3;
      ic1 = 2.0 * v1 - ic1 + dc;
      ic2 = 2.0 * v2 - ic2 + dc;
      out[i] = float(v2 / 65536.0);
    }
  } else {
    for (int i = 0; i < kBlock; ++i) out[i] = float(double(acc[i]) / 134217728.0);
  }
  if (dbg.tapLog) {
    for (int i = 0; i < kBlock && dbg.tapCount < dbg.tapCap; ++i) dbg.tapLog[dbg.tapCount++] = tap[i];
  }
  ++blockCount;
}

// ---- Engine (public) ---------------------------------------------------

Engine::Engine() : impl_(new Impl) {}
Engine::~Engine() { delete impl_; }
void Engine::prepare(double r) { impl_->prepare(r); }
double Engine::rate() const { return impl_->rate; }

void Engine::setVoice(const uint8_t* v) {
  std::memcpy(impl_->voice, v, 156);
  impl_->voiceDirty = true;
}

void Engine::setFunction(const FunctionState& f) {
  Impl& d = *impl_;
  if (f.mono != d.fn.mono) {
    for (auto& v : d.voices) {
      if (!v.active) continue;
      v.keyDown = false;
      v.sustained = false;
      for (auto& o : v.op) o.env.keyOff();
      v.peg.keydown(false);
    }
    d.monoCount = 0;
  }
  d.fn = f;
  d.refreshControllers();
}

void Engine::setMasterTune(int v) { impl_->masterTune = v < 0 ? 0 : (v > 127 ? 127 : v); }

void Engine::setMode(EngineMode m, bool dac, bool classic) {
  impl_->mode = m;
  impl_->dac = dac;
  impl_->classicVelocity = classic;
}

void Engine::noteOn(int note, int velocity) {
  if (note < 0 || note > 127) return;
  impl_->noteOn(note, velocity);
}
void Engine::noteOff(int note) { impl_->noteOff(note); }

void Engine::sustain(bool on) {
  Impl& d = *impl_;
  d.sustainOn = on;
  if (on) return;
  for (auto& v : d.voices) {
    if (!v.active || !v.sustained) continue;
    v.sustained = false;
    for (auto& o : v.op) o.env.keyOff();
    v.peg.keydown(false);
  }
}

void Engine::portamentoSwitch(bool on) { impl_->portaSwitch = on; }
void Engine::pitchBend(int v) { impl_->bend = v < 0 ? 0 : (v > 16383 ? 16383 : v); }
void Engine::modWheel(int v) {
  impl_->modwheel = v;
  impl_->refreshControllers();
}
void Engine::breath(int v) {
  impl_->breathCc = v;
  impl_->refreshControllers();
}
void Engine::foot(int v) {
  impl_->footCc = v;
  impl_->refreshControllers();
}
void Engine::aftertouch(int v) {
  impl_->atCc = v;
  impl_->refreshControllers();
}

void Engine::allNotesOff() {
  Impl& d = *impl_;
  for (auto& v : d.voices)
    if (v.active && v.keyDown) d.releaseVoice(v);
  d.monoCount = 0;
}

void Engine::allSoundOff() {
  Impl& d = *impl_;
  for (auto& v : d.voices) {
    v.active = v.keyDown = v.sustained = false;
    for (auto& o : v.op) o.env.silence();
  }
  d.monoCount = 0;
}

void Engine::render(int32_t* tap, float* out) { impl_->render(tap, out); }

void Engine::takeOperatorPeaks(int32_t peak[6]) {
  for (int k = 0; k < 6; ++k) {
    peak[k] = impl_->peaks[5 - k];
    impl_->peaks[5 - k] = 0;
  }
}

int Engine::activeVoices() const {
  int n = 0;
  for (const auto& v : impl_->voices) n += v.active ? 1 : 0;
  return n;
}

EngineDebug& Engine::debug() { return impl_->dbg; }
Engine::Impl& Engine::impl() { return *impl_; }

}  // namespace vdx::fm

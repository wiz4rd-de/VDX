// Tier 1: engine behaviour measured on the internal-rate output (SPEC §13.1).
#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "engine_harness.h"

using namespace vdx;
using namespace vdx::fm;
using namespace vdxtest;
namespace P = vdx::params;

namespace {

struct Bench {
  Engine e;
  explicit Bench(const Voice& v, EngineMode m = EngineMode::MkI, bool dac = false) {
    e.setVoice(v.data());
    e.setMode(m, dac, false);
  }
  Rendered run(double seconds) { return renderBlocks(e, blocksFor(seconds)); }
};

double semitones(int32_t dlog) { return double(dlog) / double(1 << 24) * 12.0; }

int32_t op1Logfreq(Engine& e) { return e.impl().voices[0].op[5].lastLogfreq; }

}  // namespace

TEST_CASE("AC-E3.3 (T1-1) output level steps are 0.75 dB for OL 20-99 and follow the LUT below", "[tier1][level]") {
  // The internal level is checked for every OL. The RMS step is checked
  // wherever the 14-bit Mk I operator output resolves it (peak >= 32 LSB,
  // OL >= 64 for the 6-carrier test voice); see DECISIONS.md.
  double prevDb = 0;
  int prevL = 0;
  for (int ol = 0; ol <= 99; ++ol) {
    Voice v = testVoice();
    opp(v, 1, P::kOpMode) = 1;  // fixed 1 kHz
    opp(v, 1, P::kOpCoarse) = 3;
    opp(v, 1, P::kOpFine) = 0;
    opp(v, 1, P::kOpLevel) = uint8_t(ol);
    Bench b(v);
    b.e.noteOn(60, 100);
    b.run(0.1);  // settle
    const Rendered r = b.run(0.5);
    const int L = b.e.impl().voices[0].op[5].env.level();
    const int lut[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
    const int expectedL = std::max(16, 4032 + 32 * (ol < 20 ? lut[ol] : 28 + ol) - 4256);
    INFO("OL " << ol);
    CHECK(L == expectedL);
    if (ol >= 21) CHECK(L - prevL == 32);  // 32 counts = 0.7526 dB
    const double d = db(rms(r.out.data(), r.out.size()));
    const int att = (4096 - L) * 4 + 2647;  // + carrierAtt for C = 6
    const bool resolved = att <= 8 * 1024;  // y14 peak >= 32
    if (ol >= 21 && resolved) {
      INFO("RMS step " << d - prevDb);
      CHECK(std::fabs((d - prevDb) - 0.75) <= 0.05);
    }
    if (ol >= 21) CHECK(d >= prevDb);
    prevDb = d;
    prevL = L;
  }
}

TEST_CASE("AC-E5.2 velocity sensitivity and classic velocity scaling", "[tier1][level]") {
  auto levelAt = [](int kvs, int vel, bool classic) {
    Voice v = testVoice();
    opp(v, 1, P::kOpKvs) = uint8_t(kvs);
    Engine e;
    e.setVoice(v.data());
    e.setMode(EngineMode::MkI, false, classic);
    e.noteOn(60, vel);
    renderBlocks(e, 20);
    return e.impl().voices[0].op[5].env.level();
  };
  const int ref = levelAt(0, 100, false);
  CHECK(std::abs(levelAt(7, 100, false) - ref) <= 1);
  CHECK(levelAt(7, 127, false) > levelAt(7, 100, false));
  CHECK(levelAt(7, 30, false) < levelAt(7, 100, false));
  // classic scaling: 127 -> 100
  Voice v = testVoice();
  opp(v, 1, P::kOpKvs) = 7;
  Engine a, b;
  a.setVoice(v.data());
  b.setVoice(v.data());
  a.setMode(EngineMode::MkI, true, true);
  b.setMode(EngineMode::MkI, true, false);
  a.noteOn(60, 127);
  b.noteOn(60, 100);
  const Rendered ra = renderBlocks(a, 200), rb = renderBlocks(b, 200);
  CHECK(ra.tap == rb.tap);
  CHECK(ra.out == rb.out);
}

TEST_CASE("AC-E5.3 key scaling curves match R-E5 in counts", "[tier1][level]") {
  for (int curve = 0; curve < 4; ++curve) {
    for (int note = 60 - 24; note <= 60 + 24; ++note) {
      Voice v = testVoice();
      opp(v, 1, P::kOpBreakPoint) = 39;
      opp(v, 1, P::kOpLeftDepth) = 99;
      opp(v, 1, P::kOpRightDepth) = 99;
      opp(v, 1, P::kOpLeftCurve) = uint8_t(curve);
      opp(v, 1, P::kOpRightCurve) = uint8_t(curve);
      opp(v, 1, P::kOpLevel) = 70;
      Engine e;
      e.setVoice(v.data());
      e.noteOn(note, 100);
      renderBlocks(e, 10);
      // Independent evaluation of R-E5 (MSFA dx7note.cc ScaleLevel).
      const int offset = note - 39 - 17;
      int group, depth = 99, c = curve;
      group = offset >= 0 ? (offset + 1) / 3 : -(offset - 1) / 3;
      static const int expData[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33,
                                      39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
      int scale = (c == 0 || c == 3) ? (group * depth * 329) >> 12 : (expData[std::min(group, 32)] * depth * 329) >> 15;
      if (c < 2) scale = -scale;
      int out = std::min(127, (28 + 70) + scale) * 32;
      int expected = ((scaleoutlevel(99) >> 1) << 6) + out - 4256;
      expected = std::max(16, std::min(4095, expected));
      INFO("curve " << curve << " note " << note);
      CHECK(e.impl().voices[0].op[5].env.level() == expected);
    }
  }
}

TEST_CASE("AC-E6.1 (T1-9) Mk I logfreq is always a multiple of 2^14", "[tier1][freq]") {
  Rng rng(1);
  std::vector<int32_t> log(1 << 16);
  for (int c = 0; c < 100; ++c) {
    Voice v = initVoice();
    for (int i = 0; i < 155; ++i)
      if (i < 145) v[size_t(i)] = uint8_t(rng.range(0, P::voiceMax(i)));
    v[155] = 0x3F;
    Engine e;
    e.setVoice(v.data());
    e.setMode(EngineMode::MkI, rng.range(0, 1) != 0, false);
    FunctionState f;
    f.bendRange = rng.range(0, 12);
    f.bendStep = rng.range(0, 12);
    f.wheelAssign = rng.range(0, 7);
    e.setFunction(f);
    e.setMasterTune(rng.range(0, 127));
    e.pitchBend(rng.range(0, 16383));
    e.modWheel(rng.range(0, 127));
    e.debug().logfreqLog = log.data();
    e.debug().logfreqCap = int(log.size());
    e.debug().logfreqCount = 0;
    e.noteOn(rng.range(0, 127), rng.range(1, 127));
    renderBlocks(e, 40);
    REQUIRE(e.debug().logfreqCount > 0);
    bool ok = true;
    for (int i = 0; i < e.debug().logfreqCount; ++i) ok = ok && (log[size_t(i)] % (1 << 14) == 0);
    INFO("case " << c);
    CHECK(ok);
  }
}

TEST_CASE("AC-E6.2 Modern tuning: A440, fixed 100 Hz, master tune +73.83 cents", "[tier1][freq]") {
  {
    Bench b(testVoice(), EngineMode::Modern);
    b.e.noteOn(69, 100);
    b.run(0.05);
    const Rendered r = b.run(1.0);
    CHECK(std::fabs(zeroCrossFreq(r.out.data(), r.out.size(), kInternalRate) / 440.0 - 1.0) < 0.001);
  }
  {
    Voice v = testVoice();
    opp(v, 1, P::kOpMode) = 1;
    opp(v, 1, P::kOpCoarse) = 2;
    opp(v, 1, P::kOpFine) = 0;
    Bench b(v, EngineMode::Modern);
    b.e.noteOn(60, 100);
    b.run(0.05);
    const Rendered r = b.run(2.0);
    CHECK(std::fabs(zeroCrossFreq(r.out.data(), r.out.size(), kInternalRate) / 100.0 - 1.0) < 0.001);
  }
  {
    Bench a(testVoice(), EngineMode::Modern), b(testVoice(), EngineMode::Modern);
    b.e.setMasterTune(127);
    a.e.noteOn(69, 100);
    b.e.noteOn(69, 100);
    a.run(0.05);
    b.run(0.05);
    const Rendered ra = a.run(1.0), rb = b.run(1.0);
    const double fa = zeroCrossFreq(ra.out.data(), ra.out.size(), kInternalRate);
    const double fb = zeroCrossFreq(rb.out.data(), rb.out.size(), kInternalRate);
    const double cents = 1200.0 * std::log2(fb / fa);
    CHECK(std::fabs(cents - 73.83) <= 0.5);
  }
}

TEST_CASE("AC-E6.3 transpose 48 folds note 120 back to note 120", "[tier1][freq]") {
  Voice t = testVoice();
  t[P::kTranspose] = 48;
  Bench a(t, EngineMode::Modern), b(testVoice(), EngineMode::Modern);
  a.e.noteOn(120, 100);
  b.e.noteOn(120, 100);
  a.run(0.02);
  b.run(0.02);
  CHECK(a.e.impl().voices[0].playNote == 120);
  CHECK(op1Logfreq(a.e) == op1Logfreq(b.e));
}

TEST_CASE("AC-E6.4 bend step 2 gives only multiples of 2 semitones", "[tier1][freq]") {
  Bench b(testVoice(), EngineMode::Modern);
  FunctionState f;
  f.bendRange = 12;
  f.bendStep = 2;
  b.e.setFunction(f);
  b.e.noteOn(60, 100);
  b.run(0.01);
  const int32_t base = op1Logfreq(b.e);
  std::map<int, int> seen;
  for (int bend = 0; bend <= 16383; bend += 97) {
    b.e.pitchBend(bend);
    renderBlocks(b.e, 1);
    const double st = semitones(op1Logfreq(b.e) - base);
    const double k = std::round(st / 2.0) * 2.0;
    INFO("bend " << bend << " -> " << st << " semitones");
    CHECK(std::fabs(st - k) < 1e-5);
    seen[int(k)]++;
  }
  CHECK(seen.size() >= 12);
}

TEST_CASE("AC-E7.1 (T1-6) LFO period follows the MSFA table", "[tier1][lfo]") {
  for (int speed : {0, 25, 50, 63, 75, 99}) {
    Lfo lfo;
    lfo.init(kInternalRate);
    lfo.reset(speed, 0, 0, 2);  // saw up: one wrap per period
    // measure the time of several wraps of the phase
    const double expected = 1.0 / tab::lfoSource[speed];
    const int periods = speed < 10 ? 2 : 20;
    uint32_t prev = lfo.phase();
    int wraps = 0;
    int64_t firstWrap = -1, lastWrap = -1;
    for (int64_t blk = 0; wraps <= periods && blk < 20000000; ++blk) {
      lfo.getsample();
      if (lfo.phase() < prev) {
        if (firstWrap < 0) firstWrap = blk;
        else ++wraps;
        lastWrap = blk;
      }
      prev = lfo.phase();
    }
    const double measured = double(lastWrap - firstWrap) * kBlock / kInternalRate / double(wraps);
    INFO("speed " << speed << " period " << measured << " expected " << expected);
    CHECK(std::fabs(measured / expected - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E7.2 (T1-7) pitch EG levels give the specified offsets", "[tier1][peg]") {
  const int levels[5] = {0, 17, 50, 82, 99};
  const double expect[5] = {-48.0, -12.375, 0.0, 12.0, 47.625};
  Bench ref(testVoice(), EngineMode::Modern);
  ref.e.noteOn(60, 100);
  ref.run(0.05);
  const int32_t base = op1Logfreq(ref.e);
  for (int i = 0; i < 5; ++i) {
    Voice v = testVoice();
    for (int k = 0; k < 4; ++k) v[size_t(P::kPegL1 + k)] = uint8_t(levels[i]);
    Bench b(v, EngineMode::Modern);
    b.e.noteOn(60, 100);
    b.run(1.0);
    const double st = semitones(op1Logfreq(b.e) - base);
    INFO("level " << levels[i] << " -> " << st);
    CHECK(std::fabs(st - expect[i]) <= 0.05);
  }
}

TEST_CASE("AC-E7.3 S/H follows the LCG and key sync restarts the phase", "[tier1][lfo]") {
  Lfo lfo;
  lfo.init(kInternalRate);
  lfo.reset(99, 99, 1, 5);
  lfo.keydown();
  CHECK(lfo.phase() == (1u << 31) - 1);
  uint8_t x = lfo.randstate();
  int steps = 0;
  int32_t last = 0;
  for (int blk = 0; steps < 16 && blk < 100000; ++blk) {
    const uint32_t before = lfo.phase();
    last = lfo.getsample();
    if (lfo.phase() < before) {  // wrapped -> new step
      x = uint8_t((x * 179 + 17) & 0xFF);
      ++steps;
      CHECK(lfo.randstate() == x);
      CHECK(last == ((int32_t(x ^ 0x80) + 1) << 16));
    }
  }
  CHECK(steps == 16);
  // through the engine: a new note with no other key held resets the phase
  Voice v = testVoice();
  v[P::kLfoWave] = 5;
  v[P::kLfoKeySync] = 1;
  v[P::kLfoSpeed] = 60;
  Engine e;
  e.setVoice(v.data());
  e.noteOn(60, 100);
  renderBlocks(e, 50);
  e.noteOff(60);
  renderBlocks(e, 5);
  e.noteOn(62, 100);
  CHECK(e.impl().lfo.phase() == (1u << 31) - 1);
}

TEST_CASE("AC-E8.2 (T1-8) feedback: clean at FB 0, monotonic 2nd harmonic, no fs/2 line at FB 7", "[tier1][fb]") {
  // OP1 cannot carry feedback in any algorithm; the test voice's lone carrier
  // with a feedback loop is OP6 in algorithm 32 (see DECISIONS.md).
  double prevH2 = -1;
  const double f0 = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
  for (int fb = 0; fb <= 7; ++fb) {
    Voice v = testVoice();
    opp(v, 1, P::kOpLevel) = 0;
    opp(v, 6, P::kOpLevel) = 99;
    v[P::kFeedback] = uint8_t(fb);
    Bench b(v, EngineMode::Modern);
    b.e.noteOn(60, 100);
    b.run(0.1);
    const Rendered r = b.run(0.5);
    const size_t n = 16384;
    const float* x = r.out.data() + (r.out.size() - n);
    const double h1 = toneAmplitude(x, n, f0, kInternalRate);
    const double h2 = toneAmplitude(x, n, 2 * f0, kInternalRate);
    if (fb == 0) {
      double harm = 0;
      for (int k = 2; k <= 10; ++k) {
        const double a = toneAmplitude(x, n, k * f0, kInternalRate);
        harm += a * a;
      }
      CHECK(db(std::sqrt(harm) / h1) < -60.0);
    } else {
      INFO("FB " << fb << " h2 " << h2);
      CHECK(h2 > prevH2);
    }
    prevH2 = h2;
    if (fb == 7) {
      std::vector<float> d(n);
      for (size_t i = 2; i < n; ++i) d[i] = x[i] - x[i - 2];
      // y[n] - y[n-2] removes DC and boosts fs/2; a period-2 oscillation would show here.
      double nyq = 0;
      for (size_t i = 2; i < n; ++i) nyq += (i & 1 ? -1.0 : 1.0) * double(d[i]);
      nyq = std::fabs(nyq) / double(n);
      double ref = 0;
      for (size_t i = 2; i < n; ++i) ref = std::max(ref, std::fabs(double(d[i])));
      CHECK(db(nyq / ref) < -60.0);
    }
  }
}

TEST_CASE("AC-E1.5 (T1-12) coarse 31 at note 84 aliases to 49096 - f", "[tier1][alias]") {
  Voice v = testVoice();
  opp(v, 1, P::kOpCoarse) = 31;
  Bench b(v, EngineMode::Modern);
  b.e.noteOn(84, 100);
  b.run(0.05);
  const Rendered r = b.run(0.5);
  const size_t n = 16384;
  const auto mag = spectrum(r.out.data(), n);
  size_t best = 1;
  for (size_t i = 1; i < mag.size(); ++i)
    if (mag[i] > mag[best]) best = i;
  // parabolic interpolation
  const double a = mag[best - 1], bb = mag[best], c = mag[best + 1];
  const double frac = 0.5 * (a - c) / (a - 2 * bb + c);
  const double f = (double(best) + frac) * kInternalRate / double(n);
  const double expected = 49096.0 - 31.0 * 440.0 * std::pow(2.0, (84 - 69) / 12.0);
  INFO("peak " << f << " expected " << expected);
  CHECK(std::fabs(f / expected - 1.0) <= 0.002);
}

TEST_CASE("AC-E11.1 voice stealing takes the earliest note-on, or a released voice", "[tier1][voices]") {
  Engine e;
  Voice v = testVoice();
  e.setVoice(v.data());
  for (int i = 0; i < 16; ++i) e.noteOn(40 + i, 100);
  renderBlocks(e, 2);
  e.noteOn(80, 100);
  int idx80 = -1;
  for (int i = 0; i < 16; ++i)
    if (e.impl().voices[i].note == 80) idx80 = i;
  REQUIRE(idx80 >= 0);
  bool has40 = false;
  for (int i = 0; i < 16; ++i) has40 = has40 || (e.impl().voices[i].active && e.impl().voices[i].note == 40);
  CHECK_FALSE(has40);  // note 40 was the earliest
  // release one (note 50) -> the next note steals it
  e.noteOff(50);
  renderBlocks(e, 1);
  e.noteOn(81, 100);
  bool has50 = false;
  for (int i = 0; i < 16; ++i) has50 = has50 || (e.impl().voices[i].active && e.impl().voices[i].note == 50);
  CHECK_FALSE(has50);
  bool has41 = false;
  for (int i = 0; i < 16; ++i) has41 = has41 || (e.impl().voices[i].active && e.impl().voices[i].note == 41);
  CHECK(has41);
}

TEST_CASE("AC-E11.2 a repeated note gets its own voice; note-off releases the older", "[tier1][voices]") {
  Engine e;
  e.setVoice(testVoice().data());
  e.noteOn(60, 100);
  renderBlocks(e, 2);
  e.noteOn(60, 100);
  renderBlocks(e, 2);
  int held = 0;
  uint64_t olderSerial = ~0ull;
  for (auto& v : e.impl().voices)
    if (v.active && v.note == 60) {
      ++held;
      olderSerial = std::min(olderSerial, v.serial);
    }
  REQUIRE(held == 2);
  e.noteOff(60);
  for (auto& v : e.impl().voices) {
    if (!v.active || v.note != 60) continue;
    if (v.serial == olderSerial) CHECK_FALSE(v.keyDown);
    else CHECK(v.keyDown);
  }
}

namespace {
double currentSemis(Engine& e, int voice = 0) {
  const auto& v = e.impl().voices[voice];
  return semitones(noteLogfreq(v.playNote) + v.porta - noteLogfreq(60));
}
int stageOp1(Engine& e) { return e.impl().voices[0].op[5].env.stage(); }
}  // namespace

TEST_CASE("AC-E12.1 mono FINGERED glides only on legato; FULL TIME always; release returns to held note",
          "[tier1][mono]") {
  for (int mode = 0; mode < 2; ++mode) {
    Voice v = testVoice();
    opp(v, 1, P::kOpL2) = 50;  // so stage changes are visible
    opp(v, 1, P::kOpR2) = 99;
    Engine e;
    e.setVoice(v.data());
    e.setMode(EngineMode::Modern, false, false);
    FunctionState f;
    f.mono = 1;
    f.portaMode = mode;
    f.portaTime = 40;
    e.setFunction(f);
    // non-legato first note, then a non-legato second note
    e.noteOn(60, 100);
    renderBlocks(e, 100);
    e.noteOff(60);
    renderBlocks(e, 10);
    e.noteOn(72, 100);
    renderBlocks(e, 1);
    const bool glidedNonLegato = std::fabs(currentSemis(e) - 12.0) > 0.01;
    CHECK(glidedNonLegato == (mode == 1));  // FULL TIME glides on non-legato too
    renderBlocks(e, 2000);
    REQUIRE(std::fabs(currentSemis(e) - 12.0) < 0.01);
    // legato: hold 72, press 60 -> glide, no retrigger
    const int stageBefore = stageOp1(e);
    const uint64_t serial = e.impl().voices[0].serial;
    e.noteOn(60, 100);
    renderBlocks(e, 1);
    CHECK(std::fabs(currentSemis(e) - 0.0) > 0.01);  // gliding
    CHECK(e.impl().voices[0].serial == serial);       // EGs not retriggered
    CHECK(stageOp1(e) >= stageBefore);
    renderBlocks(e, 2000);
    CHECK(std::fabs(currentSemis(e)) < 0.01);
    // release the top note -> back to the held note (72)
    e.noteOff(60);
    renderBlocks(e, 2000);
    CHECK(std::fabs(currentSemis(e) - 12.0) < 0.01);
    CHECK(e.impl().voices[0].keyDown);
  }
}

TEST_CASE("AC-E12.2 poly RETAIN keeps sustained voices' pitch; FOLLOW glides them", "[tier1][porta]") {
  for (int mode = 0; mode < 2; ++mode) {
    Engine e;
    e.setVoice(testVoice().data());
    FunctionState f;
    f.portaMode = mode;
    f.portaTime = 40;
    e.setFunction(f);
    e.sustain(true);
    e.noteOn(60, 100);
    renderBlocks(e, 50);
    e.noteOff(60);  // held by sustain
    renderBlocks(e, 5);
    int sustainedIdx = -1;
    for (int i = 0; i < 16; ++i)
      if (e.impl().voices[i].active && e.impl().voices[i].sustained) sustainedIdx = i;
    REQUIRE(sustainedIdx >= 0);
    e.noteOn(67, 100);
    renderBlocks(e, 3000);
    const double st = currentSemis(e, sustainedIdx);
    INFO("mode " << mode << " sustained voice at " << st);
    if (mode == 0) CHECK(std::fabs(st) < 0.01);
    else CHECK(std::fabs(st - 7.0) < 0.01);
  }
}

TEST_CASE("AC-E12.3 portamento time 50 glides 12 semitones in 12/(2100*2^-3.1) s; CC65 off: none",
          "[tier1][porta]") {
  Engine e;
  e.setVoice(testVoice().data());
  FunctionState f;
  f.portaTime = 50;
  e.setFunction(f);
  e.noteOn(60, 100);
  renderBlocks(e, 10);
  e.noteOff(60);
  e.noteOn(72, 100);
  int vi = -1;
  for (int i = 0; i < 16; ++i)
    if (e.impl().voices[i].keyDown && e.impl().voices[i].note == 72) vi = i;
  REQUIRE(vi >= 0);
  int blocks = 0;
  while (e.impl().voices[vi].porta != 0 && blocks < 100000) {
    renderBlocks(e, 1);
    ++blocks;
  }
  const double seconds = double(blocks) * kBlock / kInternalRate;
  const double expected = 12.0 / (2100.0 * std::pow(2.0, -3.1));
  INFO("glide " << seconds << " s, expected " << expected);
  CHECK(std::fabs(seconds / expected - 1.0) <= 0.05);
  // switch off
  e.portamentoSwitch(false);
  e.noteOn(60, 100);
  renderBlocks(e, 1);
  int idx = -1;
  for (int i = 0; i < 16; ++i)
    if (e.impl().voices[i].keyDown && e.impl().voices[i].note == 60) idx = i;
  REQUIRE(idx >= 0);
  CHECK(e.impl().voices[idx].porta == 0);
}

TEST_CASE("AC-E13.1 wheel assignments act in MSFA's direction and are 0 at wheel 0", "[tier1][ctl]") {
  for (int assign : {1, 2, 4}) {
    Voice v = testVoice();
    v[P::kLfoPmd] = 99;
    v[P::kLfoAmd] = 99;
    Engine e;
    e.setVoice(v.data());
    FunctionState f;
    f.wheelRange = 99;
    f.wheelAssign = assign;
    e.setFunction(f);
    e.modWheel(0);
    auto& d = e.impl();
    CHECK(d.pitchMod == 0);
    CHECK(d.ampMod == 0);
    CHECK(d.egMod == (assign == 4 ? 0 : 127));
    e.modWheel(127);
    if (assign == 1) CHECK(d.pitchMod == 127 * 99 / 100);
    if (assign == 2) CHECK(d.ampMod == 127 * 99 / 100);
    if (assign == 4) CHECK(d.egMod == 127 * 99 / 100);
    if (assign != 1) CHECK(d.pitchMod == 0);
    if (assign != 2) CHECK(d.ampMod == 0);
  }
}

TEST_CASE("AC-E14.1 engine and DAC toggles keep the note sounding and apply at the next block", "[tier1][mode]") {
  Engine a, b;
  const Voice v = testVoice();
  a.setVoice(v.data());
  b.setVoice(v.data());
  a.setMode(EngineMode::MkI, true, false);
  b.setMode(EngineMode::MkI, true, false);
  a.noteOn(60, 100);
  b.noteOn(60, 100);
  renderBlocks(a, 50);
  renderBlocks(b, 50);
  b.setMode(EngineMode::Modern, false, false);
  const Rendered ra = renderBlocks(a, 1), rb = renderBlocks(b, 1);
  CHECK(ra.out != rb.out);  // changed at this block
  CHECK(b.activeVoices() == 1);
  CHECK(rms(rb.out.data(), rb.out.size()) > 0.01);
  b.setMode(EngineMode::MkI, false, false);
  const Rendered rc = renderBlocks(b, 1);
  CHECK(rms(rc.out.data(), rc.out.size()) > 0.01);
  b.setMode(EngineMode::MkI, true, false);
  const Rendered rd = renderBlocks(b, 1);
  CHECK(rms(rd.out.data(), rd.out.size()) > 0.01);
}

TEST_CASE("AC-E3.1 (T1-11) Mk I and Modern agree in RMS and partials", "[tier1][modes]") {
  Rng rng(1);
  std::vector<int> algs;
  for (int a = 0; a < 32; ++a) {
    const int c = carrierCount(a);
    if (c == 1 || c == 2 || c == 4) algs.push_back(a);
  }
  for (int k = 0; k < 20; ++k) {
    Voice v = initVoice();
    const int alg = algs[size_t(rng.range(0, int(algs.size()) - 1))];
    v[P::kAlgorithm] = uint8_t(alg);
    v[P::kFeedback] = 0;
    for (int op = 1; op <= 6; ++op) {
      const int idx = 6 - op;
      const bool carrier = (algorithmFlags(alg, idx) & 3) == 0;
      opp(v, op, P::kOpCoarse) = uint8_t(rng.range(1, 4));
      // modulators 40..80 (AC: <= 80); below 40 the 14-bit Mk I output has < 5 bits (DECISIONS.md)
      opp(v, op, P::kOpLevel) = uint8_t(carrier ? rng.range(85, 99) : rng.range(40, 80));
    }
    for (int note : {48, 60, 72}) {
      Engine a, b;
      a.setVoice(v.data());
      b.setVoice(v.data());
      a.setMode(EngineMode::MkI, false, false);
      a.debug().noQuantise = true;
      a.debug().forceNoDac = true;
      b.setMode(EngineMode::Modern, false, false);
      a.noteOn(note, 100);
      b.noteOn(note, 100);
      renderBlocks(a, 200);
      renderBlocks(b, 200);
      const Rendered ra = renderBlocks(a, 256), rb = renderBlocks(b, 256);
      const double dA = db(rms(ra.out.data(), ra.out.size())), dB = db(rms(rb.out.data(), rb.out.size()));
      INFO("voice " << k << " alg " << alg + 1 << " note " << note << " rms " << dA << " vs " << dB);
      CHECK(std::fabs(dA - dB) < 0.1);
      const size_t n = 16384;
      const auto sa = spectrum(ra.out.data(), n), sb = spectrum(rb.out.data(), n);
      double mx = 0;
      for (double m : sb) mx = std::max(mx, m);
      int checked = 0, bad = 0;
      for (size_t i = 2; i + 2 < sb.size(); ++i) {
        const bool peak = sb[i] >= sb[i - 1] && sb[i] >= sb[i + 1] && sb[i] > mx * 0.01;  // > -40 dB
        if (!peak) continue;
        ++checked;
        const double ea = std::sqrt(sa[i - 1] * sa[i - 1] + sa[i] * sa[i] + sa[i + 1] * sa[i + 1]);
        const double eb = std::sqrt(sb[i - 1] * sb[i - 1] + sb[i] * sb[i] + sb[i + 1] * sb[i + 1]);
        if (std::fabs(db(ea / eb)) > 1.0) {
          ++bad;
          std::printf("AC-E3.1 voice %d alg %d note %d: bin %zu (%.1f Hz) %.2f dB rel, diff %.2f dB\n", k, alg + 1, note, i,
                      double(i) * kInternalRate / double(n), db(eb / mx), db(ea / eb));
        }
      }
      CHECK(checked > 0);
      CHECK(bad == 0);
    }
  }
}

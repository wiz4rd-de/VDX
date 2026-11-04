// Tier 1 through the public Instrument: latency, block sizes, real-time safety,
// output stage (SPEC §13.1).
#include <catch_amalgamated.hpp>

#include <cfenv>
#include <cmath>
#include <cstring>
#include <vector>

#include "core.h"
#include "engine_harness.h"
#include "rt_hooks.h"
#include "vdx/vdx.h"

using namespace vdx;
using namespace vdxtest;

namespace {

void noteOn(Instrument& inst, int note, int vel, int offset = 0, int ch = 0) {
  const uint8_t m[3] = {uint8_t(0x90 | ch), uint8_t(note), uint8_t(vel)};
  inst.midi(m, 3, offset);
}
void noteOff(Instrument& inst, int note, int offset = 0, int ch = 0) {
  const uint8_t m[3] = {uint8_t(0x80 | ch), uint8_t(note), 0};
  inst.midi(m, 3, offset);
}
void cc(Instrument& inst, int num, int val, int offset = 0) {
  const uint8_t m[3] = {0xB0, uint8_t(num), uint8_t(val)};
  inst.midi(m, 3, offset);
}

std::vector<float> renderInst(Instrument& inst, int total, int block, std::vector<float>* right = nullptr) {
  std::vector<float> L(static_cast<size_t>(total)), R(static_cast<size_t>(total));
  for (int done = 0; done < total; done += block) {
    const int n = std::min(block, total - done);
    inst.render(L.data() + done, R.data() + done, n);
  }
  if (right) *right = R;
  return L;
}

// First crossing of |x| > thr, linearly interpolated, in samples.
double onset(const std::vector<float>& x, double thr) {
  for (size_t i = 1; i < x.size(); ++i)
    if (std::fabs(x[i]) > thr) {
      const double a = std::fabs(x[i - 1]), b = std::fabs(x[i]);
      return double(i - 1) + (thr - a) / (b - a);
    }
  return -1;
}

}  // namespace

TEST_CASE("AC-E1.1 L == R and onset delay equals latencySamples() at 44.1/48/96 kHz", "[tier1][instrument]") {
  // Reference: the engine at the internal rate with the first-run voice (INIT VOICE, Mk I, DAC on).
  fm::Engine ref;
  const Voice iv = initVoice();
  ref.setVoice(iv.data());
  ref.setMode(EngineMode::MkI, true, false);
  ref.noteOn(60, 100);
  const Rendered r = renderBlocks(ref, 40);
  const double thr = 0.2 * peakAbs(r.out.data(), r.out.size());
  const double tRef = onset(r.out, thr) / kInternalRate;
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    Instrument inst(rate, 512);
    noteOn(inst, 60, 100, 0);
    std::vector<float> R;
    const auto L = renderInst(inst, int(rate * 0.25), 512, &R);
    CHECK(L == R);
    const double tOut = onset(L, thr) / rate;
    const double delay = (tOut - tRef) * rate;
    INFO("rate " << rate << " delay " << delay << " latency " << inst.latencySamples());
    CHECK(std::fabs(delay - inst.latencySamples()) <= 1.0);
  }
}

TEST_CASE("AC-E1.2 render at host rate keeps decay slopes within 2 % at 48 kHz", "[tier1][instrument]") {
  for (int rate : {0, 32, 63, 94}) {  // qrate 0, 20, 40, 60
    Voice v = testVoice();
    opp(v, 1, params::kOpL2) = 0;
    opp(v, 1, params::kOpR2) = uint8_t(rate);
    fm::Engine e;
    e.prepare(48000.0);
    e.setVoice(v.data());
    e.noteOn(60, 100);
    const int q = (rate * 41) >> 6;
    const double expectDbPerSec = 0.2819 * std::pow(2.0, q / 4) * (1.0 + 0.25 * (q % 4));
    const double dbPerCount = 6.0205999132796239 / 256.0;
    // run until in segment 2, then measure the time for ~20 dB
    auto level = [&] { return e.impl().voices[0].op[5].env.level(); };
    auto stage = [&] { return e.impl().voices[0].op[5].env.stage(); };
    int32_t tap[kBlock];
    float out[kBlock];
    while (stage() == 0) e.render(tap, out);
    const int l0 = level();
    int64_t blocks = 0;
    const int target = l0 - int(20.0 / dbPerCount);
    while (level() > target && blocks < 20000000) {
      e.render(tap, out);
      ++blocks;
    }
    const double seconds = double(blocks * kBlock) / 48000.0;
    const double measured = double(l0 - level()) * dbPerCount / seconds;
    INFO("qrate " << q << " measured " << measured << " expected " << expectDbPerSec);
    CHECK(std::fabs(measured / expectDbPerSec - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E10.1 output is clipped to 1.0, volume 0 is silent, volume 64 is (64/127)^2", "[tier1][instrument]") {
  {
    Instrument inst(48000.0, 512);
    // 16 loud voices: every operator a carrier (algorithm 32), OL 99, KVS 7, velocity 127
    for (int op = 1; op <= 6; ++op) {
      Command c;
      c.type = CmdType::SetVoiceParam;
      c.src = Source::Programmer;
      c.a = params::opParam(op, params::kOpLevel);
      c.b = 99;
      inst.post(c);
      c.a = params::opParam(op, params::kOpKvs);
      c.b = 7;
      inst.post(c);
    }
    Command alg;
    alg.type = CmdType::SetVoiceParam;
    alg.src = Source::Programmer;
    alg.a = params::kAlgorithm;
    alg.b = 31;
    inst.post(alg);
    for (int i = 0; i < 16; ++i) noteOn(inst, 40 + i * 3, 127, i);
    const auto L = renderInst(inst, 48000, 512);
    CHECK(peakAbs(L.data(), L.size()) <= 1.0);
  }
  auto rmsAtVolume = [](int vol) {
    Instrument inst(48000.0, 512);
    cc(inst, 7, vol);
    noteOn(inst, 60, 100, 1);
    renderInst(inst, 4800, 512);
    const auto L = renderInst(inst, 24000, 512);
    return rms(L.data(), L.size());
  };
  CHECK(rmsAtVolume(0) == 0.0);
  const double ratioDb = db(rmsAtVolume(64) / rmsAtVolume(127));
  CHECK(std::fabs(ratioDb - db((64.0 / 127.0) * (64.0 / 127.0))) <= 0.01);
}

#if !defined(__EMSCRIPTEN__)
TEST_CASE("AC-E15.1 (T1-13) no subnormal filter or resampler state during a 60 s decay", "[tier1][instrument][native]") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    Instrument inst(rate, 512);
    noteOn(inst, 60, 127);
    std::vector<float> L(512), R(512);
    int blocksWithUnderflow = 0;
    bool subnormalState = false;
    const int total = int(rate * 60.0) / 512;
    for (int b = 0; b < total; ++b) {
      if (b == int(rate * 0.2) / 512) noteOff(inst, 60);
      std::feclearexcept(FE_ALL_EXCEPT);
      inst.render(L.data(), R.data(), 512);
      if (std::fetestexcept(FE_UNDERFLOW)) ++blocksWithUnderflow;
      auto& eng = inst.core().engine().impl();
      if (std::fpclassify(eng.ic1) == FP_SUBNORMAL || std::fpclassify(eng.ic2) == FP_SUBNORMAL) subnormalState = true;
      for (float x : L)
        if (std::fpclassify(x) == FP_SUBNORMAL) subnormalState = true;
    }
    INFO("rate " << rate);
    CHECK(blocksWithUnderflow == 0);
    CHECK_FALSE(subnormalState);
  }
}

TEST_CASE("AC-E15.2 no allocation, lock or log call inside render() and midi()", "[tier1][instrument][native]") {
  Instrument inst(48000.0, 512);
  std::vector<float> L(512), R(512);
  rt::Counters total;
  // a bank dump to send through midi()
  std::vector<uint8_t> bank(4104, 0);
  bank[0] = 0xF0;
  bank[1] = 0x43;
  bank[2] = 0x00;
  bank[3] = 0x09;
  bank[4] = 0x20;
  bank[5] = 0x00;
  bank[4103] = 0xF7;
  const int blocks = 48000 * 10 / 512;
  for (int b = 0; b < blocks; ++b) {
    // commands (producer side, outside the measured section)
    Command c;
    c.src = Source::Programmer;
    switch (b % 8) {
      case 0: c.type = CmdType::SetVoiceParam; c.a = b % 145; c.b = b % 8; break;
      case 1: c.type = CmdType::SetSetting; c.a = int(SettingId::Engine); c.b = (b / 8) & 1; break;
      case 2: c.type = CmdType::SetSetting; c.a = int(SettingId::Dac); c.b = (b / 16) & 1; break;
      case 3: c.type = CmdType::Button; c.src = Source::Panel; c.a = 1 + b % 42; c.b = 1; break;
      case 4: c.type = CmdType::Button; c.src = Source::Panel; c.a = 1 + (b - 1) % 42; c.b = 0; break;
      case 5: c.type = CmdType::SetSetting; c.a = int(SettingId::HostRate); c.b = (b / 40) & 1; break;
      case 6: c.type = CmdType::DataEntry; c.src = Source::Panel; c.f = float(b % 100) / 100.f; break;
      default: c.type = CmdType::RequestSnapshot; break;
    }
    inst.post(c);
    Event e;
    while (inst.pollEvent(e))
      if (e.slot != 0xFFFFFFFFu) inst.readPayload(e, nullptr, 0);
    inst.readMidiOut(nullptr, 0);
    rt::begin();
    noteOn(inst, 36 + b % 48, 100, b % 512);
    if (b % 3 == 0) noteOff(inst, 36 + (b + 20) % 48, 100);
    if (b % 50 == 0) inst.midi(bank.data(), int(bank.size()), 0);
    cc(inst, 1, b % 128, 5);
    inst.render(L.data(), R.data(), 512);
    const rt::Counters c1 = rt::end();
    total.allocs += c1.allocs;
    total.locks += c1.locks;
    total.logBytes += c1.logBytes;
  }
  INFO("probes: " << rt::coverage());
  CHECK(total.allocs == 0);
  CHECK(total.locks == 0);
  CHECK(total.logBytes == 0);
}
#endif

TEST_CASE("AC-E15.3 DAC tap is identical for host block sizes 1, 7, 64, 513, 8192; n = 0 renders nothing",
          "[tier1][instrument]") {
  struct Ev {
    int64_t t;
    uint8_t m[3];
  };
  std::vector<Ev> evs;
  for (int k = 0; k < 40; ++k) {
    evs.push_back({int64_t(k) * 2311 + 17, {0x90, uint8_t(48 + k % 24), uint8_t(60 + k)}});
    evs.push_back({int64_t(k) * 2311 + 1500, {0x80, uint8_t(48 + k % 24), 0}});
  }
  std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
  const int total = 48000 * 2;
  auto run = [&](int block, bool zeroCalls) {
    Instrument inst(48000.0, 512);
    std::vector<int32_t> taps(size_t(total) * 2);
    auto& dbg = inst.core().engine().debug();
    dbg.tapLog = taps.data();
    dbg.tapCap = taps.size();
    dbg.tapCount = 0;
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    size_t next = 0;
    for (int64_t h = 0; h < total; h += block) {
      const int n = int(std::min<int64_t>(block, total - h));
      while (next < evs.size() && evs[next].t < h + n) {
        inst.midi(evs[next].m, 3, int(evs[next].t - h));
        ++next;
      }
      if (zeroCalls) {
        float sentinel = 12345.f;
        const size_t before = dbg.tapCount;
        inst.render(&sentinel, &sentinel, 0);
        CHECK(sentinel == 12345.f);
        CHECK(dbg.tapCount == before);
      }
      inst.render(L.data(), R.data(), n);
    }
    taps.resize(dbg.tapCount);
    return taps;
  };
  const auto refTaps = run(64, false);
  REQUIRE(refTaps.size() > 1000);
  for (int block : {1, 7, 64, 513, 8192}) {
    const auto t = run(block, block == 7);
    const size_t n = std::min(t.size(), refTaps.size());
    INFO("block " << block);
    CHECK(n + 64 >= refTaps.size());
    CHECK(std::equal(t.begin(), t.begin() + long(n), refTaps.begin()));
  }
}

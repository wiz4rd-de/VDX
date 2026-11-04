// Tier 1: tables (R-E3), envelope (R-E4), DAC quantiser (R-E9), algorithms (R-E8).
#include <catch_amalgamated.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine_harness.h"
#include "fm/env.h"

using namespace vdx;
using namespace vdx::fm;
using namespace vdxtest;

TEST_CASE("AC-E3.2 generated tables have the specified anchors", "[tier1][tables]") {
  CHECK(kExptab[0] == 8192);
  CHECK(kExptab[1023] == 4099);
  CHECK(kLogsin[1023] == 0);
  CHECK(kLogsin[0] == 10597);
}

TEST_CASE("AC-E3.2 committed tables equal their regeneration from the formulas", "[tier1][tables]") {
  auto rnd = [](double x) { return int64_t(std::floor(x + 0.5)); };
  const double pi = 3.14159265358979323846;
  for (int i = 0; i < 1024; ++i) {
    INFO("i=" << i);
    CHECK(int64_t(kLogsin[i]) == rnd(-1024.0 * std::log2(std::sin((i + 0.5) / 1024.0 * pi / 2.0))));
    CHECK(int64_t(kExptab[i]) == rnd(std::pow(2.0, 13.0 - i / 1024.0)));
  }
  for (int i = 0; i < 1025; ++i)
    CHECK(int64_t(kFreqlut49096[i]) == rnd(double(1ll << 44) / 49096.0 * std::pow(2.0, i / 1024.0)));
  for (int f = 0; f < 100; ++f)
    CHECK(int64_t(kFinetab[f]) == rnd(double(1 << 24) / std::log(2.0) * std::log(1.0 + 0.01 * f)));
}

TEST_CASE("AC-E9.1 DAC quantiser values", "[tier1][dac]") {
  // s -> clamp to 15 bits, sign + 11-bit mantissa + 2-bit exponent
  const int32_t in[] = {0, 2047, 2048, 4095, 8191, 16383, 40000};
  const int32_t pos[] = {0, 2047, 2048, 4094, 8188, 16376, 16376};
  const int32_t neg[] = {0, -2047, -2048, -4094, -8188, -16376, -16384};
  for (int i = 0; i < 7; ++i) {
    INFO("s=" << in[i]);
    CHECK(dacQuantise(in[i]) == pos[i]);
    CHECK(dacQuantise(-in[i]) == neg[i]);
  }
}

TEST_CASE("AC-E8.1 carriers derived from the FmAlgorithm table equal the R-E8 list", "[tier1][alg]") {
  const int expected[32] = {2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6};
  for (int a = 0; a < 32; ++a) {
    INFO("algorithm " << a + 1);
    CHECK(carrierCount(a) == expected[a]);
  }
}

namespace {
// Envelope driven by the engine-wide clock, one clock per internal sample.
struct EnvRun {
  Env e;
  uint32_t c = 0;
  void clocks(int n) {
    for (int i = 0; i < n; ++i) e.clock(c++);
  }
};
int outlevelFor(int ol) { return scaleoutlevel(ol) * 32; }  // KVS 0, no scaling
// Formula of AC-E4.1 (counts/s): 0.2819 dB/s * 2^floor(q/4) * (1 + q%4/4).
double formulaDbPerSec(int q) { return 0.2819 * std::pow(2.0, q / 4) * (1.0 + 0.25 * (q % 4)); }
constexpr double kDbPerCount = 6.0205999132796239 / 256.0;
}  // namespace

// Measures the R2 decay slope (dB/s) over whole gate cycles spanning about
// the first 20 dB. clocksPerSecond = EG clocks per second of real time.
double measureDecaySlope(int rate, double clocksPerSecond, int& qOut) {
  EnvRun r;
  const int rates[4] = {99, rate, 99, 99};
  const int levels[4] = {99, 0, 0, 0};
  r.e.init(rates, levels, outlevelFor(99), 0);
  while (r.e.stage() == 0) r.clocks(1);
  const int q = r.e.qrate();
  qOut = q;
  const uint32_t T = q < 48 ? (1u << (11 - (q >> 2))) : 1u;
  const int step = q < 48 ? 1 : 1 << ((q >> 2) - 11);
  const int active = q % 4 == 0 ? 4 : q % 4 == 1 ? 5 : q % 4 == 2 ? 6 : 7;
  const double countsPerCycle = double(step * active);
  const double goal = std::ceil(20.0 / kDbPerCount);
  const uint32_t cycles = uint32_t(std::max(1.0, std::floor(goal / countsPerCycle)));
  while (r.c % (8 * T) != 0) r.clocks(1);
  const int l0 = r.e.level();
  r.clocks(int(cycles * 8 * T));
  const int l1 = r.e.level();
  const double seconds = double(cycles * 8 * T) / clocksPerSecond;
  return double(l0 - l1) * kDbPerCount / seconds;
}

TEST_CASE("AC-E4.1 (T1-2) decay slope over the first 20 dB follows the formula for every rate", "[tier1][env]") {
  for (int rate = 0; rate <= 99; ++rate) {
    int q = 0;
    const double dbPerSec = measureDecaySlope(rate, 49096.0, q);
    REQUIRE(q == ((rate * 41) >> 6));
    const double expect = formulaDbPerSec(q);
    INFO("rate " << rate << " qrate " << q << " measured " << dbPerSec << " expected " << expect);
    CHECK(std::fabs(dbPerSec / expect - 1.0) <= 0.02);
  }
}

TEST_CASE("AC-E4.2 (T1-3) qrate 0 changes once per 4096 samples and gate masks match", "[tier1][env]") {
  // rate -> qrate: 0->0, 2->1, 4->2, 5->3
  const int rateFor[4] = {0, 2, 4, 5};
  const uint8_t masks[4] = {0x55, 0x57, 0x77, 0x7F};
  for (int q = 0; q < 4; ++q) {
    EnvRun r;
    const int rates[4] = {99, rateFor[q], 99, 99};
    const int levels[4] = {99, 0, 0, 0};
    r.e.init(rates, levels, outlevelFor(99), 0);
    while (r.e.stage() == 0) r.clocks(1);
    REQUIRE(r.e.qrate() == q);
    // align to the start of an 8-tick cycle (T = 2048 at qrate 0..3)
    while (r.c % (8 * 2048) != 0) r.clocks(1);
    int changes = 0;
    uint8_t pattern = 0;
    for (int tick = 0; tick < 8; ++tick) {
      const int before = r.e.level();
      r.clocks(2048);
      const bool changed = r.e.level() != before;
      changes += changed ? 1 : 0;
      pattern = uint8_t(pattern << 1 | (changed ? 1 : 0));
    }
    INFO("qrate " << q);
    CHECK(pattern == masks[q]);
    if (q == 0) {
      // 2^16 samples -> on average one change per 4096 samples
      EnvRun s;
      s.e.init(rates, levels, outlevelFor(99), 0);
      while (s.e.stage() == 0) s.clocks(1);
      const int before = s.e.level();
      int n = 0;
      int prev = before;
      for (int i = 0; i < 65536; ++i) {
        s.clocks(1);
        if (s.e.level() != prev) ++n;
        prev = s.e.level();
      }
      CHECK(n == 16);
    }
  }
}

TEST_CASE("AC-E4.3 (T1-4) attack is faster than decay; rate-99 attack settles within 200 samples", "[tier1][env]") {
  for (int rate = 0; rate <= 99; ++rate) {
    const int rates[4] = {rate, rate, 99, 99};
    const int levels[4] = {99, 0, 0, 0};
    EnvRun r;
    r.e.init(rates, levels, outlevelFor(99), 0);
    int64_t attack = 0;
    while (r.e.stage() == 0 && attack < 400000000) {
      r.clocks(1);
      ++attack;
    }
    int64_t decay = 0;
    while (r.e.stage() == 1 && decay < 400000000) {
      r.clocks(1);
      ++decay;
    }
    INFO("rate " << rate << " attack " << attack << " decay " << decay);
    CHECK(attack < decay);
    if (rate == 99) {
      EnvRun s;
      s.e.init(rates, levels, outlevelFor(99), 0);
      int n = 0;
      while (std::fabs(double(3840 - s.e.level())) * kDbPerCount > 0.1 && n < 1000) {
        s.clocks(1);
        ++n;
      }
      CHECK(n <= 200);
    }
  }
}

TEST_CASE("AC-E4.4 hold lengths for equal start and target", "[tier1][env]") {
  for (int rate : {0, 50, 76, 77, 99}) {
    // L1 99 -> 3840; L2 99 is equal: segment 2 holds.
    const int rates[4] = {99, rate, 99, 99};
    const int levels[4] = {99, 99, 0, 0};
    EnvRun r;
    r.e.init(rates, levels, outlevelFor(99), 0);
    while (r.e.stage() == 0) r.clocks(1);
    REQUIRE(r.e.stage() == 1);
    int64_t n = 0;
    while (r.e.stage() == 1 && n < 10000000) {
      r.clocks(1);
      ++n;
    }
    const int64_t expected = rate < 77 ? (int64_t(tab::statics[rate]) * 49096 + 22050) / 44100 : 20 * (99 - rate);
    INFO("rate " << rate << " hold " << n << " expected " << expected);
    CHECK(std::llabs(n - expected) <= 1);
  }
}

TEST_CASE("AC-E5.1 (T1-5) L 99, OL 99, velocity 100 peaks at 3840 counts and never goes below 16", "[tier1][env]") {
  Engine e;
  Voice v = testVoice();
  e.setVoice(v.data());
  e.setMode(EngineMode::MkI, false, false);
  e.noteOn(60, 100);
  int maxL = 0, minL = 1 << 30;
  for (int b = 0; b < 400; ++b) {
    int32_t tap[kBlock];
    float out[kBlock];
    e.render(tap, out);
    for (int i = 0; i < kBlock; ++i) {
      const int l = e.impl().L[5][i];  // storage index 5 = OP1
      maxL = l > maxL ? l : maxL;
      if (b > 0) minL = l < minL ? l : minL;
    }
    if (b == 200) e.noteOff(60);
  }
  CHECK(maxL == 3840);
  CHECK(minL >= 16);
}

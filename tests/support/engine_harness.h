// Helpers for engine-level (Tier 1) tests: voices, rendering, analysis.
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fm/engine.h"
#include "fm/engine_impl.h"
#include "vdx/params.h"

namespace vdxtest {

using Voice = std::vector<uint8_t>;  // 156 bytes, effective values + mask

// R-P10 INIT VOICE (effective values) + mask all on.
inline Voice initVoice() {
  Voice v(156, 0);
  for (int op = 1; op <= 6; ++op) {
    uint8_t* o = v.data() + vdx::params::opBase(op);
    o[0] = o[1] = o[2] = o[3] = 99;
    o[4] = o[5] = o[6] = 99;
    o[7] = 0;
    o[18] = 1;
    o[20] = 7;
    o[16] = op == 1 ? 99 : 0;
  }
  for (int i = 126; i < 130; ++i) v[size_t(i)] = 99;
  for (int i = 130; i < 134; ++i) v[size_t(i)] = 50;
  v[134] = 0;
  v[136] = 1;
  v[137] = 35;
  v[141] = 1;
  v[143] = 3;
  v[144] = 24;
  const char* name = "INIT VOICE";
  for (int i = 0; i < 10; ++i) v[size_t(145 + i)] = uint8_t(name[i]);
  v[155] = 0x3F;
  return v;
}

// SPEC §13 reference test voice: INIT VOICE, algorithm 32, OP1 only.
inline Voice testVoice() {
  Voice v = initVoice();
  v[vdx::params::kAlgorithm] = 31;
  return v;
}

inline uint8_t& opp(Voice& v, int op, int offset) { return v[size_t(vdx::params::opParam(op, offset))]; }

struct Rendered {
  std::vector<float> out;
  std::vector<int32_t> tap;
};

inline Rendered renderBlocks(vdx::fm::Engine& e, int blocks) {
  Rendered r;
  r.out.resize(size_t(blocks) * vdx::kBlock);
  r.tap.resize(size_t(blocks) * vdx::kBlock);
  for (int b = 0; b < blocks; ++b) e.render(r.tap.data() + size_t(b) * vdx::kBlock, r.out.data() + size_t(b) * vdx::kBlock);
  return r;
}

inline int blocksFor(double seconds, double rate = vdx::kInternalRate) { return int(std::ceil(seconds * rate / vdx::kBlock)); }

inline double rms(const float* x, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; ++i) s += double(x[i]) * double(x[i]);
  return std::sqrt(s / double(n));
}
inline double db(double x) { return 20.0 * std::log10(x > 1e-30 ? x : 1e-30); }

inline double peakAbs(const float* x, size_t n) {
  double p = 0;
  for (size_t i = 0; i < n; ++i) p = std::fabs(double(x[i])) > p ? std::fabs(double(x[i])) : p;
  return p;
}

// Frequency by rising zero crossings with linear interpolation.
inline double zeroCrossFreq(const float* x, size_t n, double rate) {
  double first = -1, last = -1;
  int count = 0;
  for (size_t i = 1; i < n; ++i) {
    if (x[i - 1] < 0.f && x[i] >= 0.f) {
      const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
      if (first < 0) first = t;
      else ++count;
      last = t;
    }
  }
  if (count < 1) return 0;
  return double(count) * rate / (last - first);
}

// In-place radix-2 FFT.
inline void fft(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.14159265358979323846 / double(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0);
      for (size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

// Magnitude spectrum (Hann window), n = power of two, bins 0..n/2.
inline std::vector<double> spectrum(const float* x, size_t n) {
  std::vector<std::complex<double>> a(n);
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(n));
    a[i] = double(x[i]) * w;
  }
  fft(a);
  std::vector<double> m(n / 2 + 1);
  for (size_t i = 0; i <= n / 2; ++i) m[i] = std::abs(a[i]) / double(n) * 4.0;
  return m;
}

// Goertzel magnitude (amplitude) of a sinusoid at frequency f.
inline double toneAmplitude(const float* x, size_t n, double f, double rate) {
  const double w = 2.0 * 3.14159265358979323846 * f / rate;
  double re = 0, im = 0;
  for (size_t i = 0; i < n; ++i) {
    const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(n));
    re += double(x[i]) * win * std::cos(w * double(i));
    im -= double(x[i]) * win * std::sin(w * double(i));
  }
  return std::sqrt(re * re + im * im) / double(n) * 4.0;
}

// Deterministic PRNG for tests (seeded).
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return uint32_t(s >> 11);
  }
  int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
};

}  // namespace vdxtest

// Deterministic transcendental functions for values that feed the integer
// signal path (SPEC §14 bit-exactness): built only from IEEE-754 +, -, *, /
// and exact power-of-two scaling, so every platform (x64, arm64, wasm) gives
// identical results. Accuracy ~1 ulp over the ranges VDX uses.
#pragma once

#include <cmath>

namespace vdx::fm::det {

constexpr double kLn2 = 0.69314718055994530941723212145818;

// e^x for |x| < 700.
inline double exp(double x) {
  // x = k*ln2 + r, |r| <= ln2/2
  const double kf = x / kLn2;
  int k = int(kf >= 0 ? kf + 0.5 : kf - 0.5);
  const double r = x - double(k) * kLn2;
  // Taylor series, 18 terms (|r| <= 0.347 -> error < 1e-19)
  double term = 1.0, sum = 1.0;
  for (int n = 1; n <= 18; ++n) {
    term = term * r / double(n);
    sum = sum + term;
  }
  return std::ldexp(sum, k);
}

inline double exp2(double x) { return exp(x * kLn2); }

// x^y for x > 0 via exp/log is not needed; pow2 covers the engine's uses.

}  // namespace vdx::fm::det

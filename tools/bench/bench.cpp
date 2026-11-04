// VDX benchmark (SPEC §9.1, AC-N1.1): 16 voices of the default BRASS voice
// sustained for 10 s, Mk I, DAC on, resampled to 48 kHz, 128-sample host
// blocks. Reports the median of 5 runs as % of real time and µs per block.
//
//   vdx_bench [--runs N] [--seconds S] [--limit PERCENT]
// Exit status 1 when the median exceeds --limit (default: no limit).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "vdx/vdx.h"

namespace {

double runOnce(double seconds, double& usPerBlock) {
  const double rate = 48000.0;
  const int block = 128;
  vdx::Instrument inst(rate, block);
  std::vector<float> L(block), R(block);
  // Program change 4 = internal slot 5 = BRASS (§15.3).
  const uint8_t pc[2] = {0xC0, 4};
  inst.midi(pc, 2, 0);
  inst.render(L.data(), R.data(), block);
  for (int i = 0; i < 16; ++i) {
    const uint8_t on[3] = {0x90, uint8_t(36 + i * 3), 100};
    inst.midi(on, 3, 0);
  }
  vdx::Event e;
  const int blocks = int(seconds * rate / block);
  const auto t0 = std::chrono::steady_clock::now();
  for (int b = 0; b < blocks; ++b) {
    inst.render(L.data(), R.data(), block);
    if ((b & 63) == 0)
      while (inst.pollEvent(e))
        if (e.slot != 0xFFFFFFFFu) inst.readPayload(e, nullptr, 0);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(t1 - t0).count();
  usPerBlock = elapsed * 1e6 / blocks;
  return elapsed / (double(blocks) * block / rate) * 100.0;
}

}  // namespace

int main(int argc, char** argv) {
  int runs = 5;
  double seconds = 10.0;
  double limit = -1.0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) limit = std::atof(argv[++i]);
  }
  std::vector<double> pct(static_cast<size_t>(runs)), us(static_cast<size_t>(runs));
  for (int r = 0; r < runs; ++r) {
    pct[size_t(r)] = runOnce(seconds, us[size_t(r)]);
    std::printf("run %d: %.3f %% of real time, %.2f us per 128-sample block\n", r + 1, pct[size_t(r)], us[size_t(r)]);
  }
  std::vector<double> sp = pct, su = us;
  std::sort(sp.begin(), sp.end());
  std::sort(su.begin(), su.end());
  const double medPct = sp[sp.size() / 2], medUs = su[su.size() / 2];
#if defined(__EMSCRIPTEN__)
  const char* build = "wasm";
#else
  const char* build = "native";
#endif
  std::printf("VDX bench (%s): median %.3f %% of real time, %.2f us per 128-sample block (16 voices BRASS, Mk I, DAC, 48 kHz)\n",
              build, medPct, medUs);
  if (limit > 0 && medPct >= limit) {
    std::printf("FAIL: median %.3f %% >= limit %.1f %%\n", medPct, limit);
    return 1;
  }
  return 0;
}

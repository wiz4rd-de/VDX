// AC-E1.4: bit-exact DAC tap goldens (native on every OS and in WASM).
// Regenerate (only together with an intended engine change):
//   VDX_UPDATE_GOLDENS=1 ./vdx_tests "AC-E1.4*"
#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine_harness.h"
#include "sha256.h"
#include "sysex/default_bank.h"
#include "sysex/voice.h"

using namespace vdx;
using namespace vdxtest;

namespace {

constexpr int kHeldBlocks = 614;      // round(0.8 s * 49096 / 64)
constexpr int kReleasedBlocks = 153;  // round(0.2 s * 49096 / 64)

struct Case {
  std::string name;
  std::vector<uint8_t> raw;  // 155 raw bytes
  int velocity;
};

std::vector<Case> cases() {
  std::vector<Case> out;
  for (int alg = 0; alg < 32; ++alg) {
    std::vector<uint8_t> raw(155);
    sysex::initVoiceRaw(raw.data());
    for (int op = 1; op <= 6; ++op) raw[size_t(params::opParam(op, params::kOpLevel))] = 99;
    raw[size_t(params::opParam(2, params::kOpCoarse))] = 2;
    raw[params::kAlgorithm] = uint8_t(alg);
    out.push_back({"alg" + std::to_string(alg + 1), raw, 100});
  }
  // Slots 1..9 of the default bank (INIT VOICE + the eight original voices); see DECISIONS.md.
  const uint8_t* bank = sysex::defaultBankPacked();
  for (int slot = 1; slot <= 9; ++slot) {
    sysex::Voice v;
    sysex::unpackVoice(bank + (slot - 1) * 128, v);
    for (int vel : {30, 100, 127}) {
      out.push_back({"slot" + std::to_string(slot) + "_v" + std::to_string(vel), std::vector<uint8_t>(v.raw, v.raw + 155), vel});
    }
  }
  return out;
}

std::string render(const Case& c, size_t& samples) {
  fm::Engine e;
  std::vector<uint8_t> eff(156);
  sysex::effectiveVoice(c.raw.data(), eff.data());
  eff[155] = 0x3F;
  e.setVoice(eff.data());
  e.setFunction(fm::FunctionState{});
  e.setMasterTune(64);
  e.setMode(EngineMode::MkI, true, false);
  std::vector<int32_t> tap(size_t(kHeldBlocks + kReleasedBlocks) * kBlock);
  auto& dbg = e.debug();
  dbg.tapLog = tap.data();
  dbg.tapCap = tap.size();
  e.noteOn(60, c.velocity);
  int32_t t[kBlock];
  float o[kBlock];
  for (int b = 0; b < kHeldBlocks; ++b) e.render(t, o);
  e.noteOff(60);
  for (int b = 0; b < kReleasedBlocks; ++b) e.render(t, o);
  samples = dbg.tapCount;
  // little-endian bytes
  std::vector<uint8_t> bytes(samples * 4);
  for (size_t i = 0; i < samples; ++i)
    for (int k = 0; k < 4; ++k) bytes[i * 4 + size_t(k)] = uint8_t(uint32_t(tap[i]) >> (8 * k));
  Sha256 h;
  h.update(bytes.data(), bytes.size());
  return h.hex();
}

const char* kManifest = VDX_SOURCE_DIR "/tests/golden/dac_taps.sha256";

}  // namespace

TEST_CASE("AC-E1.4 DAC tap streams are bit-identical to the committed goldens", "[tier1][golden]") {
  const auto all = cases();
  REQUIRE(all.size() == 59);
  const char* update = std::getenv("VDX_UPDATE_GOLDENS");
  if (update && std::string(update) == "1") {
    std::ofstream f(kManifest);
    f << "# name samples sha256(int32 little-endian DAC tap at 49096 Hz) — AC-E1.4\n";
    for (const auto& c : all) {
      size_t n = 0;
      const std::string h = render(c, n);
      f << c.name << " " << n << " " << h << "\n";
    }
    SUCCEED("goldens written");
    return;
  }
  std::ifstream f(kManifest);
  REQUIRE(f.good());
  std::map<std::string, std::pair<size_t, std::string>> want;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string name, hash;
    size_t n = 0;
    is >> name >> n >> hash;
    want[name] = {n, hash};
  }
  REQUIRE(want.size() == all.size());
  for (const auto& c : all) {
    size_t n = 0;
    const std::string h = render(c, n);
    INFO(c.name);
    REQUIRE(want.count(c.name) == 1);
    CHECK(n == want[c.name].first);
    CHECK(h == want[c.name].second);
  }
}

#include <catch_amalgamated.hpp>

#include <vector>

#include "vdx/vdx.h"

TEST_CASE("smoke: instrument renders silence at 48 kHz", "[smoke]") {
  vdx::Instrument inst(48000.0, 512);
  std::vector<float> l(512), r(512);
  inst.render(l.data(), r.data(), 512);
  REQUIRE(l == r);
  REQUIRE(inst.latencySamples() > 0);
}

TEST_CASE("smoke: a MIDI note through the whole pipeline is audible", "[smoke]") {
  vdx::Instrument inst(48000.0, 256);
  const uint8_t on[3] = {0x90, 60, 100};
  inst.midi(on, 3, 0);
  std::vector<float> l(256), r(256);
  float peak = 0.f;
  for (int b = 0; b < 40; ++b) {
    inst.render(l.data(), r.data(), 256);
    for (float x : l) peak = x > peak ? x : (-x > peak ? -x : peak);
  }
  REQUIRE(peak > 0.001f);
}

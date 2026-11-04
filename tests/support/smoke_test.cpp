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

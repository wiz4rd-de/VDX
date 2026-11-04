// AC-D7.1 audio part: every default-bank slot sounds at note 60, velocity 100.
// Needs the real engine (core/fm); tagged [engine] so the core-only suites
// ([panel],[sysex],[state],[midi]) stay independent of it.
#include <cmath>

#include "rig.h"

using namespace rig;
using namespace vdx;

TEST_CASE("AC-D7.1 every default voice is audible at note 60 velocity 100 (peak > -60 dBFS)", "[engine]") {
  for (int slot = 1; slot <= 32; ++slot) {
    INFO("INT " << slot);
    Instrument inst(48000.0, 512);
    Command pcCmd;
    pcCmd.type = CmdType::ProgramChange;
    pcCmd.src = Source::Host;
    pcCmd.a = slot - 1;
    inst.post(pcCmd);
    std::vector<float> L(512), R(512);
    inst.render(L.data(), R.data(), 512);
    const uint8_t on[3] = {0x90, 60, 100};
    inst.midi(on, 3, 0);
    float peak = 0.f;
    for (int b = 0; b < 48000 / 512; ++b) {  // 1 s held
      inst.render(L.data(), R.data(), 512);
      for (float x : L) peak = std::max(peak, std::fabs(x));
    }
    Event e;
    while (inst.pollEvent(e)) {
      if (e.slot != 0xFFFFFFFFu) inst.readPayload(e, nullptr, 0);
    }
    const double db = peak > 0.f ? 20.0 * std::log10(double(peak)) : -200.0;
    CHECK(db > -60.0);
  }
}

// AC-S1.1: every voice parameter 0–155 and function parameter 64–77 set through
// the core command (programmer), the front panel, SysEx parameter change and
// host automation (Source::Host commands; the AudioProcessor path is a plugin
// test) gives identical core state, with exactly one parameter-changed event
// per change.
#include <memory>
#include <random>

#include "rig.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

const std::string kPanelChars = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ -.";  // F-4.2: 39 characters

struct Change {
  bool fn;  // function parameter
  int index;
  int value;
};

// Random in-range values, seed 1, each different from the value before it.
std::vector<Change> plan() {
  std::mt19937 g(1);
  Rig ref;  // first-run values, to make every change a real change
  std::vector<int> cur(156);
  for (int i = 0; i < 155; ++i) cur[size_t(i)] = ref.raw(i);
  cur[155] = 0x3F;
  std::vector<Change> out;
  for (int i = 0; i <= kOpMask; ++i) {
    int v;
    do {
      if (i >= kName && i < kName + 10) v = kPanelChars[g() % kPanelChars.size()];
      else v = int(g() % uint32_t(voiceMax(i) + 1));
    } while (v == cur[size_t(i)]);
    cur[size_t(i)] = v;
    out.push_back({false, i, v});
  }
  for (int n = kFnFirst; n <= kFnLast; ++n) {
    int v;
    do v = int(g() % uint32_t(functionMax(n) + 1));
    while (v == functionDefault(n));
    out.push_back({true, n, v});
  }
  return out;
}

int paramEvents(const Rig& r, bool fn) {
  return r.count(fn ? EvType::FunctionParam : EvType::VoiceParam) + r.count(fn ? EvType::VoiceParam : EvType::FunctionParam);
}

// ---- the front-panel path ----

void panelVoice(Rig& r, int i, int v, int& changes) {
  auto& c = r.core();
  if (c.mode != kModeEdit) r.press(kBtnEdit);
  if (i == kOpMask) {
    for (int op = 1; op <= 6; ++op) {
      const int bit = 1 << (6 - op);
      if ((c.opMask & bit) != (v & bit)) {
        r.press(op);
        ++changes;
      }
    }
    return;
  }
  if (i >= kName && i < kName + 10) {
    r.press(32);
    for (int k = 0; k < i - kName; ++k) r.press(kBtnYes);
    const size_t pos = kPanelChars.find(char(v));
    REQUIRE(pos != std::string::npos);
    static const int extra[7] = {kBtnStore, kBtnProtInt, kBtnProtCrt, kBtnOpSelect, kBtnEdit, kBtnMemInt, kBtnMemCrt};
    r.press(pos < 32 ? int(pos) + 1 : extra[pos - 32]);
    r.press(kBtnFunction);  // leave naming
    ++changes;
    return;
  }
  if (i < 126) {
    while (c.selOp != opOfIndex(i)) r.press(kBtnOpSelect);
  }
  // Find the page: press page buttons until the page edits parameter i.
  int page = 0;
  {
    // the page number from the R-P1 map (same mapping as F-3)
    Rig probe;
    probe.press(kBtnEdit);
    if (i < 126) while (probe.core().selOp != opOfIndex(i)) probe.press(kBtnOpSelect);
    probe.core().showParamPage(i);
    page = probe.core().editPage;
  }
  r.press(page);
  for (int tries = 0; tries < 4; ++tries) {
    PageValue pv;
    REQUIRE(c.pageValue(pv));
    if (pv.index == i) break;
    r.press(page);  // cycle the sub-page
  }
  PageValue pv;
  REQUIRE(c.pageValue(pv));
  REQUIRE(pv.index == i);
  r.dataEntry(pv.style == PageValue::OnOff ? float(v) : float(v) / float(pv.max));
  ++changes;
}

void panelFunction(Rig& r, int n, int v, int& changes) {
  auto& c = r.core();
  if (c.mode != kModeFunction) r.press(kBtnFunction);
  const bool assign = n == kFnWheelAssign || n == kFnFootAssign || n == kFnBreathAssign || n == kFnAtAssign;
  if (assign) {
    const int first = 18 + 4 * ((n - kFnWheelAssign) / 2);
    for (int b = 0; b < 3; ++b) {
      if (((r.fnv(n) >> b) & 1) == ((v >> b) & 1)) continue;
      r.press(first + b);
      r.dataEntry(float((v >> b) & 1));
      ++changes;
    }
    return;
  }
  static const int page[14] = {2, 3, 4, 5, 6, 7, 17, 0, 21, 0, 25, 0, 29, 0};
  r.press(page[n - kFnFirst]);
  PageValue pv;
  REQUIRE(c.pageValue(pv));
  REQUIRE(pv.index == n);
  r.dataEntry(pv.style == PageValue::OnOff ? float(v) : float(v) / float(pv.max));
  ++changes;
}

enum class Path { Command, Panel, SysEx, Host };

void apply(Rig& r, Path p, const Change& ch) {
  INFO("parameter " << (ch.fn ? "fn " : "") << ch.index << " = " << ch.value);
  r.log.clear();
  int changes = 1;
  switch (p) {
    case Path::Command:
      if (ch.fn) r.progFn(ch.index, ch.value);
      else r.prog(ch.index, ch.value);
      break;
    case Path::Host:
      if (ch.fn) r.hostFn(ch.index, ch.value);
      else r.host(ch.index, ch.value);
      break;
    case Path::SysEx: {
      uint8_t m[7];
      sysex::buildParamChange(0, ch.fn ? 2 : 0, ch.index, ch.value, m);
      r.midi(std::vector<uint8_t>(m, m + 7));
      break;
    }
    case Path::Panel:
      changes = 0;
      if (ch.fn) panelFunction(r, ch.index, ch.value, changes);
      else panelVoice(r, ch.index, ch.value, changes);
      break;
  }
  if (ch.fn) CHECK(r.fnv(ch.index) == ch.value);
  else if (ch.index == kOpMask) CHECK(r.core().opMask == ch.value);
  else CHECK(r.raw(ch.index) == ch.value);
  // Exactly one parameter-changed event per change, for this parameter.
  CHECK(paramEvents(r, ch.fn) == changes);
  CHECK(r.countParam(ch.fn ? EvType::FunctionParam : EvType::VoiceParam, ch.index) == changes);
}

}  // namespace

TEST_CASE("AC-S1.1 four edit paths give one state (core command, front panel, SysEx, host)", "[panel]") {
  const auto changes = plan();
  REQUIRE(changes.size() == 156 + 14);
  std::vector<std::unique_ptr<Rig>> rigs;
  for (Path p : {Path::Command, Path::Panel, Path::SysEx, Path::Host}) {
    INFO("path " << int(p));
    auto r = std::make_unique<Rig>();
    r->global(Global::SysInfo, 1);  // SysEx path needs AVAIL; harmless for the others
    for (const auto& ch : changes) apply(*r, p, ch);
    rigs.push_back(std::move(r));
  }
  for (size_t k = 1; k < rigs.size(); ++k) {
    INFO("path " << k << " vs command path");
    CHECK(std::memcmp(rigs[k]->ps().edit.raw, rigs[0]->ps().edit.raw, 155) == 0);
    CHECK(std::memcmp(rigs[k]->ps().fn, rigs[0]->ps().fn, 14) == 0);
    CHECK(rigs[k]->core().opMask == rigs[0]->core().opMask);
    CHECK(rigs[k]->ps().edited == rigs[0]->ps().edited);
    CHECK(std::memcmp(rigs[k]->core().engVoice, rigs[0]->core().engVoice, 156) == 0);
  }
}

TEST_CASE("AC-U4.1 core part: SelectOperator selects the operator without changing the mode", "[panel]") {
  vdx::Instrument inst(48000.0, 512);
  std::vector<float> l(512), r(512);
  vdx::Command c;
  c.type = vdx::CmdType::SelectOperator;
  c.src = vdx::Source::Programmer;
  c.a = 4;
  inst.post(c);
  inst.render(l.data(), r.data(), 512);
  CHECK(inst.core().impl().selOp == 4);
  CHECK(inst.core().impl().mode == vdx::kModePlay);
}

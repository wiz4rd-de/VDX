// AC-P2.1 (LCD golden matrix), AC-P1.1 (button map golden), AC-P8.1 (display values).
#include <fstream>
#include <sstream>

#include "panel/format.h"
#include "rig.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

std::string trimStr(const std::string& s) {
  size_t b = s.find_first_not_of(' ');
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(' ');
  return s.substr(b, e - b + 1);
}

int buttonByName(const std::string& t) {
  if (t == "INT") return kBtnMemInt;
  if (t == "CRT") return kBtnMemCrt;
  if (t == "EDIT") return kBtnEdit;
  if (t == "FUNC") return kBtnFunction;
  if (t == "STORE") return kBtnStore;
  if (t == "PINT") return kBtnProtInt;
  if (t == "PCRT") return kBtnProtCrt;
  if (t == "OPSEL") return kBtnOpSelect;
  if (t == "NO") return kBtnNo;
  if (t == "YES") return kBtnYes;
  return 0;
}

void parseAssign(const std::string& t, int& i, int& v) {
  const size_t c = t.find(':'), e = t.find('=');
  i = std::stoi(t.substr(c + 1, e - c - 1));
  v = std::stoi(t.substr(e + 1));
}

void runStep(Rig& r, const std::string& t) {
  if (t == "-") return;
  if (const int b = buttonByName(t)) return r.press(b);
  if (std::isdigit(static_cast<unsigned char>(t[0]))) return r.press(std::stoi(t));
  int i = 0, v = 0;
  if (t.rfind("h:", 0) == 0) return parseAssign(t, i, v), r.host(i, v);
  if (t.rfind("p:", 0) == 0) return parseAssign(t, i, v), r.prog(i, v);
  if (t.rfind("f:", 0) == 0) return parseAssign(t, i, v), r.hostFn(i, v);
  if (t.rfind("g:", 0) == 0) return parseAssign(t, i, v), r.global(Global(i), v);
  if (t.rfind("d:", 0) == 0) return r.dataEntry(std::stof(t.substr(2)));
  if (t.rfind("w:", 0) == 0) return r.ms(std::stod(t.substr(2)));
  if (t.rfind("note:", 0) == 0) {
    const int n = std::stoi(t.substr(5));
    r.midi(noteOn(1, n, 100));
    r.midi(noteOff(1, n));
    return;
  }
  if (t == "crt") return r.newCartridge();
  if (t == "splash") return r.simple(CmdType::StartSplash, Source::Internal);
  if (t == "reject") return r.simple(CmdType::FileRejected, Source::BankManager);
  if (t == "badsum" || t == "rxvoice") {
    uint8_t raw[155];
    sysex::initVoiceRaw(raw);
    if (t == "badsum") {
      Command c = cmd(CmdType::LoadVoice, Source::BankManager, 0, 1);
      r.postPayload(c, raw, 155);
      r.render(64);
    } else {
      uint8_t m[163];
      sysex::buildVoiceDump(raw, 0, m);
      r.midi(std::vector<uint8_t>(m, m + 163));
    }
    return;
  }
  if (t == "reset") {
    StateBlob bad;
    bad.bytes = {'N', 'O', 'P', 'E', 1, 0, 0, 0, 0, 0, 0, 0};
    r.inst.loadState(bad);
    r.render(64);
    r.simple(CmdType::StartSplash, Source::Internal);
    r.ms(1600);
    return;
  }
  FAIL("unknown golden step: " << t);
}

std::string unquote(const std::string& s) {
  const std::string t = trimStr(s);
  REQUIRE(t.size() >= 2);
  return t.substr(1, t.size() - 2);
}

}  // namespace

TEST_CASE("AC-P2.1 LCD golden matrix (hand-written from SPEC 5.3)", "[panel]") {
  std::ifstream f(sourceDir() + "/tests/golden/lcd_matrix.txt");
  REQUIRE(f.good());
  std::string line;
  int checked = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || trimStr(line).empty()) continue;
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '|')) parts.push_back(part);
    REQUIRE(parts.size() == 5);
    const std::string id = trimStr(parts[0]);
    const std::string want1 = unquote(parts[2]), want2 = unquote(parts[3]), wantCur = trimStr(parts[4]);
    INFO("golden " << id << " (setup: " << trimStr(parts[1]) << ")");
    Rig r;
    std::stringstream steps(trimStr(parts[1]));
    std::string step;
    while (steps >> step) runStep(r, step);
    CHECK(r.l1 == want1);
    CHECK(r.l2 == want2);
    if (wantCur == "-") {
      CHECK_FALSE(r.cursor);
    } else {
      CHECK(r.cursor);
      CHECK(std::to_string(r.curRow) + "," + std::to_string(r.curCol) == wantCur);
    }
    ++checked;
  }
  CHECK(checked > 200);
}

// ---------------------------------------------------------------------------
// AC-P1.1: page and parameter shown for every button in every mode (R-P1).
// ---------------------------------------------------------------------------

namespace {

// Parameter a page edits, as "v<index>", "f<n>", "f<n>b<bit>", "g<global>" or "-".
std::string pageParam(Rig& r) {
  PageValue pv;
  if (!r.core().pageValue(pv)) return "-";
  switch (pv.kind) {
    case PageValue::Voice: return "v" + std::to_string(pv.index);
    case PageValue::Function: return "f" + std::to_string(pv.index);
    case PageValue::FnBit: return "f" + std::to_string(pv.index) + "b" + std::to_string(pv.bit);
    case PageValue::GlobalV: return "g" + std::to_string(pv.index);
    default: return "-";
  }
}

}  // namespace

TEST_CASE("AC-P1.1 EDIT mode: page and parameter for each of the 32 buttons", "[panel]") {
  // R-P1 EDIT column with the selected operator OP1 ("op k" = byte k of OP1 = 105 + k).
  const char* expect[33] = {
      nullptr,
      // 1..6: operator on/off (mask bits 5..0), page unchanged
      "mask", "mask", "mask", "mask", "mask", "mask",
      "v134", "v135", "v142", "v137", "v138", "v139", "v140", "v141", "v143",
      "v119",          // 16 A MOD SENS = op 14
      "v122",          // 17 OSC MODE = op 17 (first press: mode sub-page)
      "v123", "v124", "v125",  // 18 coarse, 19 fine, 20 detune
      "v105", "v109",  // 21 EG RATE 1 = op 0, 22 EG LEVEL 1 = op 4
      "v113",          // 23 break point = op 8
      "v116",          // 24 L curve = op 11
      "v114",          // 25 L depth = op 9
      "v118", "v121", "v120",  // 26 RS = op 13, 27 OL = op 16, 28 KVS = op 15
      "v126", "v130", "v144",  // 29 P EG rate 1, 30 P EG level 1, 31 transpose
      "name"};
  for (int b = 1; b <= 32; ++b) {
    INFO("EDIT button " << b);
    Rig r;
    r.press(kBtnEdit);
    r.press(10);  // a known previous page
    r.press(b);
    if (b <= 6) {
      CHECK(r.core().opMask == (0x3F ^ (1 << (6 - b))));
      CHECK(r.panelPage == 10);
      CHECK(r.panelMode == kModeEdit);
      CHECK(r.lampNum == (1 << 9));
    } else if (b == 32) {
      CHECK(r.panelMode == kModeEditName);
      CHECK(r.panelPage == 32);
      CHECK(r.cursor);
    } else {
      CHECK(r.panelMode == kModeEdit);
      CHECK(r.panelPage == b);
      CHECK(pageParam(r) == expect[b]);
      CHECK(r.lampNum == int(1u << (b - 1)));
      CHECK((r.lampMode & (1 << (kBtnEdit - kBtnMemInt))) != 0);
    }
  }
  // Second presses cycle the sub-pages (17 mode/sync, 21/22/29/30 stages, 24/25 L/R).
  struct Cyc { int btn; const char* second; };
  const Cyc cyc[] = {{17, "v136"}, {21, "v106"}, {22, "v110"}, {24, "v117"}, {25, "v115"}, {29, "v127"}, {30, "v131"}};
  for (const auto& c : cyc) {
    INFO("EDIT button " << c.btn << " pressed twice");
    Rig r;
    r.press(kBtnEdit);
    r.press(c.btn);
    r.press(c.btn);
    CHECK(pageParam(r) == c.second);
  }
  // Operator pages follow the selected operator.
  Rig r;
  r.press(kBtnEdit);
  r.press(27);
  r.press(kBtnOpSelect);
  r.press(kBtnOpSelect);
  CHECK(pageParam(r) == "v" + std::to_string(opParam(3, kOpLevel)));
}

TEST_CASE("AC-P1.1 FUNCTION mode: page and parameter for each of the 32 buttons", "[panel]") {
  const char* expect[33] = {nullptr,
                            "g0",  "f64", "f65", "f66", "f67", "f68", "f69",
                            "g1",  // 8: MIDI CH (sub 0)
                            "-", "-", "-",     // 9..11 questions
                            "same", "same",    // 12/13 no effect
                            "-", "-", "-",     // 14 battery, 15/16 questions
                            "f70", "f71b0", "f71b1", "f71b2", "f72", "f73b0", "f73b1", "f73b2",
                            "f74", "f75b0", "f75b1", "f75b2", "f76", "f77b0", "f77b1", "f77b2"};
  for (int b = 1; b <= 32; ++b) {
    INFO("FUNCTION button " << b);
    Rig r;
    r.press(kBtnFunction);
    r.press(7);
    r.press(b);
    CHECK(r.panelMode == kModeFunction);
    if (std::string(expect[b]) == "same") {
      CHECK(r.panelPage == 7);
      CHECK(pageParam(r) == "f69");
    } else {
      CHECK(r.panelPage == b);
      CHECK(pageParam(r) == expect[b]);
      CHECK(r.lampNum == int(1u << (b - 1)));
      CHECK((r.lampMode & (1 << (kBtnFunction - kBtnMemInt))) != 0);
    }
  }
}

TEST_CASE("AC-P1.1 PLAY mode: each button selects that slot of the selected memory", "[panel]") {
  const char* names[10] = {nullptr, "INIT VOICE", "E.PIANO   ", "BELL      ", "BASS      ", "BRASS     ",
                           "ORGAN     ", "MARIMBA   ", "STRINGS   ", "PAD       "};
  for (int b = 1; b <= 32; ++b) {
    INFO("PLAY button " << b);
    Rig r;
    r.press(b);
    CHECK(r.panelMode == kModePlay);
    CHECK(r.ps().srcBank == kBankInt);
    CHECK(r.ps().srcSlot == b);
    CHECK(r.led == b);
    CHECK(r.lampNum == int(1u << (b - 1)));
    CHECK(r.lampMode == (1 << (kBtnMemInt - kBtnMemInt)));
    const std::string name = b <= 9 ? names[b] : "INIT VOICE";
    CHECK(r.l2.substr(6) == name);
  }
  // With the cartridge selected the buttons select cartridge slots.
  Rig r;
  r.newCartridge();
  r.press(kBtnMemCrt);
  r.press(17);
  CHECK(r.ps().srcBank == kBankCrt);
  CHECK(r.ps().srcSlot == 17);
  CHECK(r.lampMode == (1 << (kBtnMemCrt - kBtnMemInt)));
}

TEST_CASE("AC-P1.1 EDIT_NAME mode: each button types its character", "[panel]") {
  const char* map = "1234567890ABCDEFGHIJKLMNOPQRSTUV";
  for (int b = 1; b <= 32; ++b) {
    INFO("EDIT_NAME button " << b);
    Rig r;
    r.press(kBtnEdit);
    r.press(32);
    r.press(b);
    CHECK(r.raw(kName) == map[b - 1]);
    CHECK(r.panelMode == kModeEditName);
    CHECK(r.panelPage == 32);
    CHECK(r.lampNum == int(1u << 31));
  }
}

// ---------------------------------------------------------------------------
// AC-P8.1: LCD field == format module == event text, every parameter at min and max.
// ---------------------------------------------------------------------------

namespace {

// Navigate to the page that shows voice parameter i (selected operator, sub-pages).
void gotoVoicePage(Rig& r, int i) {
  r.press(kBtnEdit);
  if (i < 126) {
    const int op = opOfIndex(i);
    while (r.core().selOp != op) r.press(kBtnOpSelect);
  }
  r.core().showParamPage(i);  // sub-page selection (same as the F-3 mapping)
  r.core().refresh();
  r.drain();
}

// The value field: L2 after its literal prefix (or L1 cols 3–4 / 6–11).
std::string lcdVoiceField(Rig& r, int i) {
  if (i == kAlgorithm) return r.l1.substr(3, 2);
  if (i == kOpMask) return r.l1.substr(6, 6);
  if (i >= kName && i < kName + 10) return r.l2.substr(6 + (i - kName), 1);
  const int off = i < 126 ? offsetOfIndex(i) : -1;
  if (off == kOpMode) return r.l2;
  size_t eq = r.l2.find('=');
  if (i == kTranspose) eq = r.l2.find("= ") + 1;
  REQUIRE(eq != std::string::npos);
  return r.l2.substr(eq + 1);
}

}  // namespace

TEST_CASE("AC-P8.1 voice parameter display values: LCD == format module == event text", "[panel]") {
  for (int i = 0; i <= kOpMask; ++i) {
    const int mx = i >= kName && i < kName + 10 ? 0x7E : voiceMax(i);
    const int mn = i >= kName && i < kName + 10 ? 0x20 : 0;
    for (int v : {mn, mx}) {
      INFO("voice parameter " << i << " = " << v);
      Rig r;
      // Make the starting value differ so an event is emitted.
      const int start = v == mn ? (i == kOpMask ? 0x3F : mx) : mn;
      r.host(i, start);
      r.log.clear();
      r.host(i, v);
      const Ev* ev = nullptr;
      for (const auto& x : r.log)
        if (x.e.type == EvType::VoiceParam && x.e.a == i) ev = &x;
      REQUIRE(ev != nullptr);
      CHECK(ev->e.b == v);
      uint8_t eff[156];
      r.core().editEffective(eff);
      char s[fmt::kMaxText];
      fmt::voiceString(eff, i, v, s);
      CHECK(std::string(ev->e.text) == s);
      gotoVoicePage(r, i);
      std::string field = lcdVoiceField(r, i);
      if (!(i >= kName && i < kName + 10)) field = trimStr(field);
      CHECK(field == s);
    }
  }
}

TEST_CASE("AC-P8.1 function parameter display values: LCD == format module == event text", "[panel]") {
  for (int n = kFnFirst; n <= kFnLast; ++n) {
    const bool assign = n == kFnWheelAssign || n == kFnFootAssign || n == kFnBreathAssign || n == kFnAtAssign;
    for (int v : {0, functionMax(n)}) {
      INFO("function parameter " << n << " = " << v);
      Rig r;
      r.hostFn(n, v == 0 ? functionMax(n) : 0);
      r.log.clear();
      r.hostFn(n, v);
      const Ev* ev = r.last(EvType::FunctionParam);
      REQUIRE(ev != nullptr);
      CHECK(ev->e.a == n);
      char s[fmt::kMaxText];
      fmt::functionString(r.core().ps.fn, n, v, s);
      CHECK(std::string(ev->e.text) == s);
      r.press(kBtnFunction);
      if (!assign) {
        static const int page[14] = {2, 3, 4, 5, 6, 7, 17, 0, 21, 0, 25, 0, 29, 0};
        r.press(page[n - kFnFirst]);
        std::string field = r.l2;
        const size_t eq = field.find('=');
        if (eq != std::string::npos) field = field.substr(eq + 1);
        CHECK(trimStr(field) == s);
      } else {
        // Each bit has its own page; the event text lists PITCH AMP EG-BIAS.
        const int first = 18 + 4 * ((n - kFnWheelAssign) / 2);
        std::string joined;
        for (int b = 0; b < 3; ++b) {
          r.press(first + b);
          const std::string field = trimStr(r.l2.substr(r.l2.find('=') + 1));
          char bs[fmt::kMaxText];
          fmt::assignBitString(v, b, bs);
          CHECK(field == bs);
          joined += (b ? " " : "") + field;
        }
        CHECK(joined == s);
      }
    }
  }
}

TEST_CASE("AC-P8.1 panel globals and operator switches use the same strings", "[panel]") {
  char s[fmt::kMaxText];
  fmt::globalString(int(Global::MasterTune), 127, s);
  CHECK(std::string(s) == "+73.8 cents");
  fmt::globalString(int(Global::MasterTune), 0, s);
  CHECK(std::string(s) == "-75.0 cents");
  fmt::globalString(int(Global::MasterTune), 64, s);
  CHECK(std::string(s) == "0.0 cents");
  CHECK(std::string(fmt::opOnString(true)) == "ON");
  CHECK(std::string(fmt::opOnString(false)) == "OFF");
  Rig r;
  r.global(Global::MasterTune, 127);
  const Ev* ev = r.last(EvType::GlobalValue);
  REQUIRE(ev != nullptr);
  CHECK(std::string(ev->e.text) == "+73.8 cents");
  fmt::lfoHz(35, s);
  CHECK(std::string(s) == "5.62 Hz");
  fmt::lfoHz(0, s);
  CHECK(std::string(s) == "0.06 Hz");
  fmt::lfoHz(99, s);
  CHECK(std::string(s) == "49.26 Hz");
}

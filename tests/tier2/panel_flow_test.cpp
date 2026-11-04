// Front-panel flows: AC-F1.1 … AC-F11.1, AC-F2.1 (one test per F-2 cell), AC-F2.2,
// AC-P3.1, AC-P4.1, AC-P6.1, AC-P7.1, AC-P9.1, AC-P11.1, AC-S2.1, AC-U2.1, AC-U3.1.
#include <cmath>
#include <functional>
#include <memory>

#include "rig.h"
#include "sysex/default_bank.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

constexpr int kLampEdit = 1 << (kBtnEdit - kBtnMemInt);

std::vector<uint8_t> rawVec(const sysex::Voice& v) { return std::vector<uint8_t>(v.raw, v.raw + 155); }
std::vector<uint8_t> slotRaw(const uint8_t* bank, int slot) {
  sysex::Voice v;
  sysex::unpackVoice(bank + (slot - 1) * 128, v);
  return rawVec(v);
}

// Rig in each panel mode.
void toEdit(Rig& r) { r.press(kBtnEdit); }
void toName(Rig& r) {
  r.press(kBtnEdit);
  r.press(32);
}
void toFunction(Rig& r) { r.press(kBtnFunction); }

// The sounding voice handed to the engine.
std::vector<uint8_t> sounding(Rig& r) { return std::vector<uint8_t>(r.core().engVoice, r.core().engVoice + 156); }
std::vector<uint8_t> effOf(const sysex::Voice& v, int mask) {
  std::vector<uint8_t> e(156);
  sysex::effectiveVoice(v.raw, e.data());
  e[155] = uint8_t(mask);
  return e;
}

}  // namespace

// ============================================================================
// AC-F1.1 start (core part)
// ============================================================================

TEST_CASE("AC-F1.1 splash lasts 1.5 s of rendered audio, ignores panel input, passes MIDI notes", "[panel]") {
  for (double rate : {48000.0, 44100.0, kInternalRate}) {
    INFO("host rate " << rate);
    Rig r(rate);
    r.press(kBtnEdit);  // before the splash the panel is live
    r.press(kBtnMemInt);
    r.post(cmd(CmdType::StartSplash, Source::Internal));
    r.render(1);
    REQUIRE(r.l1 == "      VDX       ");
    CHECK(r.led == 0);
    CHECK(r.panelMode == kModeSplash);
    CHECK(r.lampNum == 0);
    CHECK(r.lampMode == 0);
    long samples = 1;
    const uint32_t notesBefore = r.core().noteOnsToEngine;
    bool pressed = false;
    while (r.l1 == "      VDX       " && samples < long(rate * 3)) {
      if (!pressed && samples > long(rate * 0.5)) {
        // panel inputs during the splash are ignored; a MIDI note reaches the engine
        r.down(kBtnFunction);
        r.up(kBtnFunction);
        r.down(kBtnYes);
        r.up(kBtnYes);
        Command de = cmd(CmdType::DataEntry, Source::Panel);
        de.f = 1.f;
        r.post(de);
        const auto on = noteOn(1, 60, 100);
        r.inst.midi(on.data(), 3, 0);
        pressed = true;
      }
      r.render(32);
      samples += 32;
    }
    const double seconds = double(samples) / rate;
    CHECK(seconds == Catch::Approx(1.5).margin(0.05));
    CHECK(r.core().noteOnsToEngine == notesBefore + 1);
    // F-1.5: PLAY, defaults, nothing from the ignored inputs.
    CHECK(r.panelMode == kModePlay);
    CHECK(r.l1 == "INTERNAL VOICE  ");
    CHECK(r.l2 == "INT 1 INIT VOICE");
    CHECK(r.led == 1);
    CHECK(r.ps().fn[kFnPortaTime - kFnFirst] == 0);
    CHECK(r.core().editPage == 7);
    CHECK(r.core().fnPage == 1);
    CHECK(r.core().selOp == 1);
  }
}

TEST_CASE("AC-F1.1 after the splash: F-1.5 page defaults and selected memory = voice source bank", "[panel]") {
  Rig r;
  r.newCartridge();
  r.press(kBtnMemCrt);
  r.press(4);  // voice source CRT 4
  r.press(kBtnMemInt);
  r.press(kBtnEdit);
  r.presses({22, 22, 24, 24, 29, 17, 17, kBtnOpSelect});
  r.press(kBtnFunction);
  r.presses({8, 8});
  r.splashAndWait();
  auto& c = r.core();
  CHECK(r.panelMode == kModePlay);
  CHECK(r.ps().selMem == kBankCrt);
  CHECK(c.editPage == 7);
  CHECK(c.fnPage == 1);
  CHECK(c.selOp == 1);
  CHECK(c.egStage == 0);
  CHECK(c.pegStage == 0);
  CHECK(c.scaleSide == 0);
  CHECK(c.b17Sub == 0);
  CHECK(c.b8Sub == 0);
  CHECK(r.l1 == "CARTRIDGE VOICE ");
}

// ============================================================================
// AC-F2.1: one test per cell of the F-2 tables
// ============================================================================

// ---- MEMORY SELECT INT ----
TEST_CASE("AC-F2.1 MEMORY SELECT INT in PLAY: selected memory = INT", "[panel]") {
  Rig r;
  r.newCartridge();
  r.press(kBtnMemCrt);
  REQUIRE(r.ps().selMem == kBankCrt);
  r.press(kBtnMemInt);
  CHECK(r.ps().selMem == kBankInt);
  CHECK(r.panelMode == kModePlay);
}
TEST_CASE("AC-F2.1 MEMORY SELECT INT in EDIT: PLAY, INT, ends compare", "[panel]") {
  Rig r;
  r.newCartridge();
  r.press(kBtnMemCrt);
  r.host(kFeedback, 3);
  toEdit(r);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.press(kBtnMemInt);
  CHECK(r.panelMode == kModePlay);
  CHECK(r.ps().selMem == kBankInt);
  CHECK_FALSE(r.core().comparing);
}
TEST_CASE("AC-F2.1 MEMORY SELECT INT in EDIT_NAME: types '-'", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnMemInt);
  CHECK(r.raw(kName) == '-');
  CHECK(r.panelMode == kModeEditName);
}
TEST_CASE("AC-F2.1 MEMORY SELECT INT in FUNCTION: PLAY, INT", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnMemInt);
  CHECK(r.panelMode == kModePlay);
  CHECK(r.ps().selMem == kBankInt);
}

// ---- MEMORY SELECT CRT ----
TEST_CASE("AC-F2.1 MEMORY SELECT CRT in PLAY: CRT, or NOT READY without a cartridge", "[panel]") {
  Rig r;
  r.press(kBtnMemCrt);
  CHECK(r.l1 == "   NOT READY !  ");
  CHECK(r.ps().selMem == kBankInt);
  r.ms(2100);
  r.newCartridge();
  r.press(kBtnMemCrt);
  CHECK(r.ps().selMem == kBankCrt);
  CHECK(r.l1 == "CARTRIDGE VOICE ");
}
TEST_CASE("AC-F2.1 MEMORY SELECT CRT in EDIT: PLAY with CRT; without cartridge NOT READY and nothing else", "[panel]") {
  Rig r;
  r.host(kFeedback, 3);
  toEdit(r);
  r.press(kBtnEdit);  // comparing
  r.press(kBtnMemCrt);
  CHECK(r.l1 == "   NOT READY !  ");
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.core().comparing);
  CHECK(r.ps().selMem == kBankInt);
  r.newCartridge();
  r.press(kBtnMemCrt);
  CHECK(r.panelMode == kModePlay);
  CHECK(r.ps().selMem == kBankCrt);
  CHECK_FALSE(r.core().comparing);
}
TEST_CASE("AC-F2.1 MEMORY SELECT CRT in EDIT_NAME: types '.'", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnMemCrt);
  CHECK(r.raw(kName) == '.');
}
TEST_CASE("AC-F2.1 MEMORY SELECT CRT in FUNCTION: as EDIT", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnMemCrt);
  CHECK(r.l1 == "   NOT READY !  ");
  CHECK(r.panelMode == kModeFunction);
  r.newCartridge();
  r.press(kBtnMemCrt);
  CHECK(r.panelMode == kModePlay);
  CHECK(r.ps().selMem == kBankCrt);
}

// ---- EDIT/COMPARE ----
TEST_CASE("AC-F2.1 EDIT/COMPARE in PLAY: EDIT on the last edit page", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(23);
  r.press(kBtnMemInt);
  r.press(kBtnEdit);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 23);
}
TEST_CASE("AC-F2.1 EDIT/COMPARE in EDIT: no effect while CLEAN, toggles COMPARE while EDITED", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(kBtnEdit);
  CHECK_FALSE(r.core().comparing);
  CHECK(r.panelMode == kModeEdit);
  r.host(kFeedback, 4);
  r.press(kBtnEdit);
  CHECK(r.core().comparing);
  CHECK((r.lampBlink & kLampEdit) != 0);
  r.press(kBtnEdit);
  CHECK_FALSE(r.core().comparing);
  CHECK(r.lampBlink == 0);
}
TEST_CASE("AC-F2.1 EDIT/COMPARE in EDIT_NAME: types a space", "[panel]") {
  Rig r;
  r.press(2);
  toName(r);
  r.press(kBtnEdit);
  CHECK(r.raw(kName) == ' ');
  CHECK(r.panelMode == kModeEditName);
}
TEST_CASE("AC-F2.1 EDIT/COMPARE in FUNCTION: EDIT on the last edit page", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(12);
  r.press(kBtnFunction);
  r.press(kBtnEdit);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 12);
}

// ---- FUNCTION ----
TEST_CASE("AC-F2.1 FUNCTION in PLAY: FUNCTION on the last function page", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(6);
  r.press(kBtnMemInt);
  r.press(kBtnFunction);
  CHECK(r.panelMode == kModeFunction);
  CHECK(r.panelPage == 6);
}
TEST_CASE("AC-F2.1 FUNCTION in EDIT: FUNCTION, ends compare", "[panel]") {
  Rig r;
  r.host(kFeedback, 4);
  toEdit(r);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.press(kBtnFunction);
  CHECK(r.panelMode == kModeFunction);
  CHECK_FALSE(r.core().comparing);
}
TEST_CASE("AC-F2.1 FUNCTION in EDIT_NAME: EDIT, VOICE NAME page, no cursor", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnFunction);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 32);
  CHECK_FALSE(r.cursor);
  CHECK(r.l2 == "NAME= INIT VOICE");
}
TEST_CASE("AC-F2.1 FUNCTION in FUNCTION: no effect", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(7);
  r.press(kBtnFunction);
  CHECK(r.panelMode == kModeFunction);
  CHECK(r.panelPage == 7);
}

// ---- STORE ----
TEST_CASE("AC-F2.1 STORE in PLAY: store dialog", "[panel]") {
  Rig r;
  r.press(kBtnStore);
  CHECK(r.core().dialog == Dialog::Store);
  CHECK(r.l2 == " MEMORY STORE   ");
  CHECK((r.lampMode & (1 << (kBtnStore - kBtnMemInt))) != 0);
}
TEST_CASE("AC-F2.1 STORE in EDIT: EG-copy dialog; no effect while comparing", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(kBtnStore);
  CHECK(r.core().dialog == Dialog::EgCopy);
  CHECK((r.lampMode & (1 << (kBtnStore - kBtnMemInt))) != 0);
  r.press(kBtnStore);  // cancels
  CHECK(r.core().dialog == Dialog::None);
  r.host(kFeedback, 1);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.press(kBtnStore);
  CHECK(r.core().dialog == Dialog::None);
}
TEST_CASE("AC-F2.1 STORE in EDIT_NAME: types 'W'", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnStore);
  CHECK(r.raw(kName) == 'W');
}
TEST_CASE("AC-F2.1 STORE in FUNCTION: no effect", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnStore);
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.l1 == "FUNCTION CONTROL");
}

// ---- MEMORY PROTECT INT / CRT ----
TEST_CASE("AC-F2.1 MEMORY PROTECT in PLAY: protect dialog", "[panel]") {
  Rig r;
  r.press(kBtnProtInt);
  CHECK(r.core().dialog == Dialog::ProtInt);
  CHECK((r.lampMode & (1 << (kBtnProtInt - kBtnMemInt))) != 0);
  r.press(kBtnProtCrt);
  CHECK(r.core().dialog == Dialog::ProtCrt);
  CHECK((r.lampMode & (1 << (kBtnProtCrt - kBtnMemInt))) != 0);
}
TEST_CASE("AC-F2.1 MEMORY PROTECT in EDIT: protect dialog", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(kBtnProtCrt);
  CHECK(r.core().dialog == Dialog::ProtCrt);
  CHECK(r.l1 == " MEMORY PROTECT ");
}
TEST_CASE("AC-F2.1 MEMORY PROTECT in EDIT_NAME: types 'X' / 'Y'", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnProtInt);
  r.press(kBtnProtCrt);
  CHECK(r.raw(kName) == 'X');
  CHECK(r.raw(kName + 1) == 'Y');
  CHECK(r.core().dialog == Dialog::None);
}
TEST_CASE("AC-F2.1 MEMORY PROTECT in FUNCTION: protect dialog", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnProtInt);
  CHECK(r.core().dialog == Dialog::ProtInt);
}

// ---- OPERATOR SELECT ----
TEST_CASE("AC-F2.1 OPERATOR SELECT in PLAY: no effect", "[panel]") {
  Rig r;
  r.press(kBtnOpSelect);
  CHECK(r.core().selOp == 1);
  CHECK(r.panelMode == kModePlay);
}
TEST_CASE("AC-F2.1 OPERATOR SELECT in EDIT: next enabled operator, wrapping; none enabled: no effect", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(27);
  r.press(kBtnOpSelect);
  CHECK(r.core().selOp == 2);
  CHECK(r.l1 == "ALG 1 111111 OP2");
  r.presses({3, 4});  // OP3, OP4 off
  r.press(kBtnOpSelect);
  CHECK(r.core().selOp == 5);
  r.presses({kBtnOpSelect, kBtnOpSelect});
  CHECK(r.core().selOp == 1);  // 6 -> wraps to 1
  r.presses({1, 2, 5, 6});     // all off
  r.press(kBtnOpSelect);
  CHECK(r.core().selOp == 1);
  CHECK(r.l1 == "ALG 1 000000 OP1");
}
TEST_CASE("AC-F2.1 OPERATOR SELECT in EDIT_NAME: types 'Z'", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnOpSelect);
  CHECK(r.raw(kName) == 'Z');
}
TEST_CASE("AC-F2.1 OPERATOR SELECT in FUNCTION: no effect", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnOpSelect);
  CHECK(r.core().selOp == 1);
}

// ---- Buttons 1–32 ----
TEST_CASE("AC-F2.1 buttons 1-32 in PLAY: select that slot of the selected memory", "[panel]") {
  Rig r;
  r.press(6);
  CHECK(r.ps().srcSlot == 6);
  CHECK(r.l2 == "INT 6 ORGAN     ");
  r.newCartridge();
  r.press(kBtnMemCrt);
  r.press(30);
  CHECK(r.ps().srcBank == kBankCrt);
  CHECK(r.ps().srcSlot == 30);
}
TEST_CASE("AC-F2.1 buttons 1-32 in EDIT: mask toggles, pages, sub-page cycling, naming; nothing while comparing",
          "[panel]") {
  Rig r;
  toEdit(r);
  r.press(9);
  r.press(1);
  CHECK(r.core().opMask == 0x1F);
  CHECK(r.panelPage == 9);
  CHECK(r.l2 == "LFO WAVE=TRIANGL");
  CHECK_FALSE(r.edited());
  r.press(21);
  r.press(21);
  CHECK(r.core().egStage == 1);
  r.press(32);
  CHECK(r.panelMode == kModeEditName);
  r.press(kBtnFunction);
  r.host(kFeedback, 1);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.press(8);
  r.press(3);
  CHECK(r.panelPage == 32);
  CHECK(r.core().opMask == 0x1F);
}
TEST_CASE("AC-F2.1 buttons 1-32 in EDIT_NAME: type the character", "[panel]") {
  Rig r;
  toName(r);
  r.press(1);
  r.press(10);
  r.press(11);
  r.press(32);
  CHECK(std::string(reinterpret_cast<const char*>(r.ps().edit.raw + kName), 4) == "10AV");
}
TEST_CASE("AC-F2.1 buttons 1-32 in FUNCTION: pages, 8 cycles, 12/13 no effect, questions", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(3);
  CHECK(r.panelPage == 3);
  r.press(12);
  CHECK(r.panelPage == 3);
  r.press(13);
  CHECK(r.panelPage == 3);
  r.press(8);
  CHECK(r.l2 == "MIDI CH= 1      ");
  r.press(8);
  CHECK(r.l2 == "SYS INFO UNAVAIL");
  r.press(8);
  CHECK(r.l2 == "MIDI CH= 1      ");  // 2 skipped while UNAVAIL
  r.global(Global::SysInfo, 1);
  r.presses({8, 8});
  CHECK(r.l2 == " MIDI TRANSMIT ?");
  r.press(8);
  CHECK(r.l2 == "MIDI CH= 1      ");
  r.press(11);
  CHECK(r.l2 == "CARTRIDGE FORM ?");
  r.press(3);
  r.press(8);
  CHECK(r.l2 == "MIDI CH= 1      ");  // returning keeps the sub-page
}

// ---- NO / YES ----
TEST_CASE("AC-F2.1 NO/YES in PLAY: no effect", "[panel]") {
  Rig r;
  const auto before = r.ps();
  r.press(kBtnYes);
  r.press(kBtnNo);
  CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
  CHECK(r.l2 == "INT 1 INIT VOICE");
}
TEST_CASE("AC-F2.1 NO/YES in EDIT: -1/+1, on/off, text choices, page 7, page 32, comparing", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(kBtnYes);
  CHECK(r.raw(kAlgorithm) == 1);  // page 7 steps the algorithm
  r.press(kBtnNo);
  r.press(kBtnNo);
  CHECK(r.raw(kAlgorithm) == 0);  // clamped
  r.press(14);
  r.press(kBtnNo);
  CHECK(r.raw(kLfoKeySync) == 0);
  r.press(kBtnNo);
  CHECK(r.raw(kLfoKeySync) == 0);
  r.press(kBtnYes);
  CHECK(r.raw(kLfoKeySync) == 1);
  r.press(9);
  r.presses({kBtnYes, kBtnYes});
  CHECK(r.l2 == "LFO WAVE=SAW UP ");
  r.press(32);
  r.press(kBtnFunction);  // page 32 outside naming
  const auto name = rawVec(r.ps().edit);
  r.press(kBtnYes);
  CHECK(rawVec(r.ps().edit) == name);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.press(8);
  r.press(kBtnYes);
  CHECK(r.raw(kFeedback) == 0);
}
TEST_CASE("AC-F2.1 NO/YES in EDIT_NAME: cursor left/right clamped to 1..10", "[panel]") {
  Rig r;
  toName(r);
  r.press(kBtnNo);
  CHECK(r.curCol == 6);
  for (int i = 0; i < 12; ++i) r.press(kBtnYes);
  CHECK(r.curCol == 15);
  r.press(kBtnNo);
  CHECK(r.curCol == 14);
}
TEST_CASE("AC-F2.1 NO/YES in FUNCTION: values, SYS INFO, answers, page 14", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(kBtnYes);
  CHECK(r.globalv(Global::MasterTune) == 65);
  r.press(3);
  r.press(kBtnNo);
  CHECK(r.fnv(kFnBendRange) == 1);
  r.presses({8, 8, kBtnYes});
  CHECK(r.globalv(Global::SysInfo) == 1);
  r.press(kBtnNo);
  CHECK(r.globalv(Global::SysInfo) == 0);
  r.presses({kBtnYes, 8});
  r.takeMidiOut();
  r.press(kBtnNo);  // TRANSMIT: NO does nothing
  CHECK(r.takeMidiOut().empty());
  r.press(kBtnYes);
  auto out = r.takeMidiOut();
  REQUIRE(out.size() == 1);
  CHECK(out[0].size() == 4104);
  CHECK(r.l2 == " COMPLETED      ");
  r.press(14);
  const auto before = r.ps();
  r.presses({kBtnYes, kBtnNo});
  CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
  r.press(10);
  r.press(kBtnYes);
  CHECK(r.l2 == " ARE YOU SURE ? ");
}

// ---- Data entry ----
TEST_CASE("AC-F2.1 data entry in PLAY: ignored", "[panel]") {
  Rig r;
  const auto before = r.ps();
  r.dataEntry(1.f);
  CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
}
TEST_CASE("AC-F2.1 data entry in EDIT: round(pos x max), on/off, choices; ignored while comparing", "[panel]") {
  Rig r;
  toEdit(r);
  r.dataEntry(0.5f);
  CHECK(r.raw(kAlgorithm) == 16);  // round(15.5)
  r.press(14);
  r.dataEntry(0.49f);
  CHECK(r.raw(kLfoKeySync) == 0);
  r.dataEntry(0.5f);
  CHECK(r.raw(kLfoKeySync) == 1);
  r.press(9);
  r.dataEntry(0.5f);
  CHECK(r.raw(kLfoWave) == 3);  // round(0.5 x 5) = 3
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.dataEntry(0.f);
  CHECK(r.raw(kLfoWave) == 3);
}
TEST_CASE("AC-F2.1 data entry in EDIT_NAME: ignored", "[panel]") {
  Rig r;
  toName(r);
  const auto before = rawVec(r.ps().edit);
  r.dataEntry(0.3f);
  CHECK(rawVec(r.ps().edit) == before);
  CHECK(r.curCol == 6);
}
TEST_CASE("AC-F2.1 data entry in FUNCTION: value pages; ignored on 8 sub 1-2 and 9-16", "[panel]") {
  Rig r;
  toFunction(r);
  r.press(7);
  r.dataEntry(1.f);
  CHECK(r.fnv(kFnPortaTime) == 99);
  r.press(1);
  r.dataEntry(0.f);
  CHECK(r.globalv(Global::MasterTune) == 0);
  r.press(8);
  r.dataEntry(1.f);
  CHECK(r.globalv(Global::MidiChannel) == 16);
  r.dataEntry(0.f);
  CHECK(r.globalv(Global::MidiChannel) == 1);  // clamped to the range
  r.press(8);
  const auto before = r.ps();
  r.dataEntry(1.f);
  for (int p : {9, 10, 11, 14, 15, 16}) {
    r.press(p);
    r.dataEntry(1.f);
    r.dataEntry(0.f);
  }
  CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
}

// ============================================================================
// AC-F2.2 general rules
// ============================================================================

TEST_CASE("AC-F2.2 a pending dialog is cancelled by another input, which then acts", "[panel]") {
  Rig r;
  r.press(kBtnStore);
  r.press(kBtnMemInt);  // cancels the store dialog, then selects INT
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.l2 == "INT 1 INIT VOICE");
  r.press(kBtnProtInt);
  r.press(kBtnEdit);  // cancels, then enters EDIT
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.panelMode == kModeEdit);
  r.press(kBtnStore);  // EG copy dialog
  r.press(10);         // cancels, then shows page 10
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.panelPage == 10);
  r.press(kBtnStore);
  r.dataEntry(1.f);  // the slider is ignored while a dialog is pending
  CHECK(r.core().dialog == Dialog::EgCopy);
  CHECK(r.raw(kLfoSpeed) == 35);
}

TEST_CASE("AC-F2.2 a message is dismissed by any input, which then acts; a new message replaces it", "[panel]") {
  Rig r;
  r.press(kBtnMemCrt);
  REQUIRE(r.l1 == "   NOT READY !  ");
  r.press(kBtnEdit);
  CHECK(r.core().msg == Msg::None);
  CHECK(r.panelMode == kModeEdit);
  r.simple(CmdType::FileRejected, Source::BankManager);
  r.ms(1500);
  r.simple(CmdType::FileRejected, Source::BankManager);  // restarts its 2 s
  r.ms(1500);
  CHECK(r.l2 == "FORMAT CONFLICT!");
  r.ms(600);
  CHECK(r.l2 == "ALGORITHM SELECT");
  r.simple(CmdType::FileRejected, Source::BankManager);
  r.press(kBtnYes);  // dismisses, then acts
  CHECK(r.raw(kAlgorithm) == 1);
  CHECK(r.l2 == "ALGORITHM SELECT");
}

TEST_CASE("AC-F2.2 holding YES for 1.5 s gives 1 + 10 +- 1 steps", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(11);  // LFO DELAY = 0
  r.down(kBtnYes);
  r.render(64);
  CHECK(r.raw(kLfoDelay) == 1);  // the press is one step
  r.ms(1500);
  r.up(kBtnYes);
  r.render(64);
  const int steps = r.raw(kLfoDelay);
  CHECK(steps >= 10);
  CHECK(steps <= 12);
  r.ms(500);
  CHECK(r.raw(kLfoDelay) == steps);  // released: no more steps
  // NO repeats as well, and the repeat clamps.
  r.down(kBtnNo);
  r.ms(3000);
  r.up(kBtnNo);
  r.render(64);
  CHECK(r.raw(kLfoDelay) == 0);
}

// ============================================================================
// AC-F3.1 programmer edits
// ============================================================================

TEST_CASE("AC-F3.1 programmer edit from PLAY: EDIT on the parameter's page, its operator, EDITED", "[panel]") {
  Rig r;
  r.prog(opParam(4, kOpLevel), 55);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 27);
  CHECK(r.core().selOp == 4);
  CHECK(r.l1 == "ALG 1 111111 OP4");
  CHECK(r.l2 == "OUTPUT LEVEL =55");
  CHECK(r.edited());
  // A function change from EDIT goes to FUNCTION on its page.
  r.progFn(kFnPortaTime, 20);
  CHECK(r.panelMode == kModeFunction);
  CHECK(r.panelPage == 7);
  CHECK(r.l2 == "PORTA TIME=20   ");
  r.progFn(kFnFootAssign, 2);  // AMP bit
  CHECK(r.panelPage == 7);    // already in FUNCTION: page unchanged
  r.press(kBtnMemInt);
  r.progFn(kFnFootAssign, 3);  // PITCH bit changed
  CHECK(r.panelPage == 22);
  // A voice change from FUNCTION goes to EDIT; sub-pages follow the parameter.
  r.prog(opParam(2, kOpL3), 10);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.l2 == "EG  LEVEL 3  =10");
  CHECK(r.l1 == "ALG 1 111111 OP2");
  // In EDIT the page stays but the selected operator follows, even if disabled.
  r.press(6);  // OP6 off
  r.prog(opParam(6, kOpL3), 11);
  CHECK(r.panelPage == 22);
  CHECK(r.core().selOp == 6);
  // A pending dialog is cancelled.
  r.press(kBtnStore);
  REQUIRE(r.core().dialog == Dialog::EgCopy);
  r.prog(kFeedback, 2);
  CHECK(r.core().dialog == Dialog::None);
  // COMPARE ends first.
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  r.prog(kFeedback, 3);
  CHECK_FALSE(r.core().comparing);
  CHECK(r.raw(kFeedback) == 3);
  // Naming ends.
  r.press(32);
  REQUIRE(r.panelMode == kModeEditName);
  r.prog(kLfoSpeed, 1);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 32);
  CHECK_FALSE(r.cursor);
  // Master tune from the programmer cancels dialogs but keeps the mode.
  r.press(kBtnProtInt);
  r.global(Global::MasterTune, 70);
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.panelMode == kModeEdit);
  // Touching without a change does nothing.
  r.press(kBtnStore);
  r.prog(kLfoSpeed, 1);
  CHECK(r.core().dialog == Dialog::EgCopy);
}

// ============================================================================
// AC-F4.1 naming (front panel part)
// ============================================================================

TEST_CASE("AC-F4.1 naming: 39 character inputs, typing past character 10, FUNCTION leaves", "[panel]") {
  const std::string map = "1234567890ABCDEFGHIJKLMNOPQRSTUV";
  struct In {
    int btn;
    char c;
  };
  std::vector<In> all;
  for (int b = 1; b <= 32; ++b) all.push_back({b, map[size_t(b - 1)]});
  all.push_back({kBtnStore, 'W'});
  all.push_back({kBtnProtInt, 'X'});
  all.push_back({kBtnProtCrt, 'Y'});
  all.push_back({kBtnOpSelect, 'Z'});
  all.push_back({kBtnEdit, ' '});
  all.push_back({kBtnMemInt, '-'});
  all.push_back({kBtnMemCrt, '.'});
  REQUIRE(all.size() == 39);
  for (const auto& in : all) {
    INFO("button " << in.btn);
    Rig r;
    toName(r);
    r.press(in.btn);
    CHECK(char(r.raw(kName)) == in.c);
    CHECK(r.curCol == 7);
  }
  Rig r;
  toName(r);
  for (int i = 0; i < 12; ++i) r.press(11 + i);  // A B C ... L
  CHECK(std::string(reinterpret_cast<const char*>(r.ps().edit.raw + kName), 10) == "ABCDEFGHIL");
  CHECK(r.curCol == 15);
  CHECK(r.edited());
  r.press(kBtnFunction);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 32);
  CHECK_FALSE(r.cursor);
  CHECK(r.l2 == "NAME= ABCDEFGHIL");
}

// ============================================================================
// AC-F5.1 key transpose
// ============================================================================

TEST_CASE("AC-F5.1 armed KEY TRANSPOSE: the next note sets transpose and does not sound", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(31);
  CHECK((r.panelFlags & kFlagTransposeArmed) != 0);
  const uint32_t before = r.core().noteOnsToEngine;
  r.midi(noteOn(1, 48, 100));
  CHECK(r.raw(kTranspose) == 12);
  CHECK(r.l2 == "MIDDLE C = C 2  ");
  CHECK(r.core().noteOnsToEngine == before);  // did not sound
  CHECK((r.panelFlags & kFlagTransposeArmed) == 0);
  r.midi(noteOff(1, 48));
  CHECK(r.core().noteOffsToEngine == 0);  // its note-off is eaten too
  r.midi(noteOn(1, 60, 100));
  CHECK(r.core().noteOnsToEngine == before + 1);
  CHECK(r.core().lastEngineNote == 60);
  CHECK(r.core().engVoice[kTranspose] == 12);  // the engine plays it 12 semitones lower
  // Pressing 31 again re-arms; leaving the page disarms; any source arms.
  r.press(31);
  r.press(30);
  r.post(cmd(CmdType::NoteOn, Source::Keyboard, 70, 100));
  r.render(64);
  CHECK(r.raw(kTranspose) == 12);
  r.press(31);
  r.post(cmd(CmdType::NoteOn, Source::Keyboard, 100, 100));
  r.render(64);
  CHECK(r.raw(kTranspose) == 48);  // clamp(100 - 36, 0, 48)
}

// ============================================================================
// AC-F6.1 … AC-F11.1
// ============================================================================

TEST_CASE("AC-F6.1 selecting INT 7 while EDITED on INT 3", "[panel]") {
  Rig r;
  r.press(3);
  r.host(opParam(1, kOpLevel), 12);
  const auto edits = rawVec(r.ps().edit);
  REQUIRE(r.edited());
  r.press(7);
  CHECK(rawVec(r.ps().edit) == slotRaw(sysex::defaultBankPacked(), 7));
  CHECK(rawVec(r.ps().stored) == slotRaw(sysex::defaultBankPacked(), 7));
  CHECK_FALSE(r.edited());
  CHECK(r.led == 7);
  CHECK(r.ledDot == 0);
  CHECK(rawVec(r.ps().recall) == edits);
  CHECK(r.ps().srcSlot == 7);
}

TEST_CASE("AC-F7.1 store to cartridge", "[panel]") {
  Rig r;
  r.newCartridge();
  r.global(Global::ProtectCrt, 0);
  r.press(5);  // BRASS from INT
  r.host(kFeedback, 1);
  r.press(kBtnMemCrt);
  r.press(kBtnStore);
  r.press(9);
  CHECK(slotRaw(r.ps().crtBank, 9) == rawVec(r.ps().edit));
  CHECK(r.ps().srcBank == kBankCrt);
  CHECK(r.ps().srcSlot == 9);
  CHECK_FALSE(r.edited());
  CHECK(r.ps().crtChanged == 1);
  CHECK(r.l2 == "CRT 9 BRASS     ");
  CHECK(r.led == 9);
  CHECK(rawVec(r.ps().stored) == rawVec(r.ps().edit));
}

TEST_CASE("AC-F8.1 EG copy from OP3", "[panel]") {
  Rig r;
  r.press(5);
  toEdit(r);
  r.presses({kBtnOpSelect, kBtnOpSelect});
  REQUIRE(r.core().selOp == 3);
  for (int k = 0; k < 8; ++k) r.host(opParam(3, k), 10 + k);
  const auto before = rawVec(r.ps().edit);
  r.press(kBtnStore);
  CHECK(r.l2 == "from OP3 to OP? ");
  r.press(5);
  for (int k = 0; k < 8; ++k) CHECK(r.raw(opParam(5, k)) == r.raw(opParam(3, k)));
  for (int k = 8; k < 21; ++k) CHECK(r.raw(opParam(5, k)) == before[size_t(opParam(5, k))]);
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.panelPage == 7);
  // Pressing 3 (the same operator) changes nothing.
  const auto now = rawVec(r.ps().edit);
  r.press(kBtnStore);
  r.press(3);
  CHECK(rawVec(r.ps().edit) == now);
  CHECK(r.core().dialog == Dialog::None);
}

TEST_CASE("AC-F9.1 compare plays the stored voice; host automation ends it and applies", "[panel]") {
  Rig r;
  r.press(4);
  r.host(opParam(1, kOpLevel), 20);
  toEdit(r);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  CHECK(sounding(r) == effOf(r.ps().stored, 0x3F));
  CHECK(r.l2 == "INT 4 BASS      ");
  CHECK(r.ledDot == 0);
  CHECK((r.lampBlink & kLampEdit) != 0);
  // Host parameters always reflect the edit buffer: no events while comparing.
  r.host(opParam(2, kOpLevel), 44);
  CHECK_FALSE(r.core().comparing);
  CHECK(r.raw(opParam(2, kOpLevel)) == 44);
  CHECK(sounding(r) == effOf(r.ps().edit, 0x3F));
  CHECK(r.panelPage == 7);
  CHECK(r.l2 == "ALGORITHM SELECT");
  CHECK(r.ledDot == 1);
}

TEST_CASE("AC-F10.1 FUNCTION confirmations with and without cartridge and protect", "[panel]") {
  struct Case {
    int page;
    bool crt;
    bool protInt, protCrt;
    const char* msg1;
    const char* msg2;
  };
  const Case cases[] = {
      {9, false, true, true, " COMPLETED      ", "EDIT RECALL ?   "},
      {10, false, true, true, " COMPLETED      ", "VOICE INIT ?    "},
      {11, false, false, false, "INSERT CARTRIDGE", "CARTRIDGE FORM ?"},
      {11, true, false, true, "MEMORY PROTECTED", "CARTRIDGE FORM ?"},
      {11, true, false, false, " COMPLETED      ", "CARTRIDGE FORM ?"},
      {15, false, false, false, "INSERT CARTRIDGE", " ALL OF MEMORY ?"},
      {15, true, false, true, "MEMORY PROTECTED", " ALL OF MEMORY ?"},
      {15, true, true, false, " COMPLETED      ", " ALL OF MEMORY ?"},
      {16, false, false, false, "INSERT CARTRIDGE", " ALL OF MEMORY ?"},
      {16, true, true, false, "MEMORY PROTECTED", " ALL OF MEMORY ?"},
      {16, true, false, true, " COMPLETED      ", " ALL OF MEMORY ?"},
  };
  for (const auto& c : cases) {
    INFO("page " << c.page << " crt " << c.crt << " protInt " << c.protInt << " protCrt " << c.protCrt);
    Rig r;
    std::vector<uint8_t> crtData(4096, 0);
    if (c.crt) {
      // A recognisable cartridge: the default bank with slot 1 renamed.
      crtData.assign(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
      std::memcpy(crtData.data() + 118, "CRT VOICE ", 10);
      std::vector<uint8_t> p = crtData;
      p.push_back(0);
      r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
      r.render(64);
    }
    r.global(Global::ProtectInt, c.protInt);
    r.global(Global::ProtectCrt, c.protCrt);
    r.press(2);
    r.host(kFeedback, 1);  // EDITED E.PIANO
    r.press(3);            // recall = edited E.PIANO; edit = BELL
    r.host(kFeedback, 2);
    const auto recall = rawVec(r.ps().recall);
    const std::vector<uint8_t> intBefore(r.ps().intBank, r.ps().intBank + 4096);
    r.press(kBtnFunction);
    r.press(c.page);
    // NO at the question and at ARE YOU SURE returns to the question.
    r.press(kBtnNo);
    CHECK(r.l2 == c.msg2);
    r.press(kBtnYes);
    CHECK(r.l2 == " ARE YOU SURE ? ");
    r.press(kBtnNo);
    CHECK(r.l2 == c.msg2);
    r.press(kBtnYes);
    r.press(kBtnYes);
    CHECK(r.l2 == c.msg1);
    r.ms(2100);
    CHECK(r.l2 == c.msg2);
    const bool ok = std::string(c.msg1) == " COMPLETED      ";
    switch (c.page) {
      case 9:
        CHECK(rawVec(r.ps().edit) == recall);
        CHECK(r.edited());
        break;
      case 10: {
        uint8_t init[155];
        sysex::initVoiceRaw(init);
        CHECK(std::memcmp(r.ps().edit.raw, init, 155) == 0);
        CHECK(r.edited());
        break;
      }
      case 11:
        if (ok) {
          for (int s = 1; s <= 32; ++s) {
            uint8_t init[155];
            sysex::initVoiceRaw(init);
            CHECK(slotRaw(r.ps().crtBank, s) == std::vector<uint8_t>(init, init + 155));
          }
          CHECK(r.ps().crtChanged == 1);
          CHECK(std::string(r.ps().crtName, r.ps().crtNameLen) == "");  // name kept (loaded without one)
        } else if (c.crt) {
          CHECK(std::memcmp(r.ps().crtBank, crtData.data(), 4096) == 0);
          CHECK(r.ps().crtChanged == 0);
        }
        break;
      case 15:
        if (ok) {
          CHECK(std::memcmp(r.ps().crtBank, r.ps().intBank, 4096) == 0);
          CHECK(r.ps().crtChanged == 1);
        } else if (c.crt) {
          CHECK(std::memcmp(r.ps().crtBank, crtData.data(), 4096) == 0);
        }
        break;
      case 16:
        if (ok) CHECK(std::memcmp(r.ps().intBank, crtData.data(), 4096) == 0);
        else CHECK(std::memcmp(r.ps().intBank, intBefore.data(), 4096) == 0);
        break;
      default: break;
    }
    if (c.page == 9 || c.page == 10) CHECK(r.core().opMask == 0x3F);
  }
}

TEST_CASE("AC-F11.1 memory protect dialog: YES on, NO off, another input ends it and acts", "[panel]") {
  Rig r;
  r.global(Global::ProtectInt, 0);
  r.press(kBtnProtInt);
  CHECK(r.l2 == " INTERNAL    OFF");
  r.press(kBtnYes);
  CHECK(r.globalv(Global::ProtectInt) == 1);
  CHECK(r.l2 == " INTERNAL    ON ");
  r.press(kBtnNo);
  CHECK(r.globalv(Global::ProtectInt) == 0);
  CHECK(r.core().dialog == Dialog::ProtInt);
  r.press(7);
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.ps().srcSlot == 7);  // button 7 acted (PLAY: selects INT 7)
  CHECK(r.l2 == "INT 7 MARIMBA   ");
}

// ============================================================================
// AC-P3.1 … AC-P11.1
// ============================================================================

TEST_CASE("AC-P3.1 slider, CC6 and YES/NO on every value page", "[panel]") {
  struct P {
    int mode;  // 1 EDIT, 3 FUNCTION
    std::vector<int> buttons;
  };
  std::vector<P> pages;
  for (int b = 7; b <= 31; ++b) pages.push_back({1, {b}});
  pages.push_back({1, {17, 17}});
  pages.push_back({1, {21, 21, 21, 21}});
  pages.push_back({1, {22, 22, 22}});
  pages.push_back({1, {24, 24}});
  pages.push_back({1, {25, 25}});
  pages.push_back({1, {29, 29}});
  pages.push_back({1, {30, 30, 30, 30}});
  for (int b : {1, 2, 3, 4, 5, 6, 7, 8, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32})
    pages.push_back({3, {b}});
  for (const auto& p : pages) {
    INFO("mode " << p.mode << " page " << p.buttons.back() << " x" << p.buttons.size());
    Rig r;
    r.press(p.mode == 1 ? kBtnEdit : kBtnFunction);
    for (int b : p.buttons) r.press(b);
    PageValue pv;
    REQUIRE(r.core().pageValue(pv));
    auto value = [&] { return r.core().getPageValue(pv); };
    const bool onOff = pv.style == PageValue::OnOff;
    auto expectAt = [&](double pos) {
      if (onOff) return pos >= 0.5 ? 1 : 0;
      int v = int(std::floor(pos * pv.max + 0.5));
      return v < pv.min ? pv.min : v;
    };
    for (float pos : {0.f, 0.5f, 1.f}) {
      r.dataEntry(pos);
      CHECK(value() == expectAt(pos));
      CHECK(r.dePos == Catch::Approx(double(value()) / pv.max).margin(1e-6));
    }
    for (int v : {0, 64, 127}) {
      r.midi(cc(1, 6, v));
      CHECK(value() == expectAt(double(float(v) / 127.f)));
    }
    // YES/NO at the limits clamp.
    r.dataEntry(1.f);
    r.press(kBtnYes);
    CHECK(value() == pv.max);
    r.midi(cc(1, 96, 127));
    CHECK(value() == pv.max);
    r.dataEntry(0.f);
    r.press(kBtnNo);
    CHECK(value() == pv.min);
    r.midi(cc(1, 97, 127));
    CHECK(value() == pv.min);
    r.press(kBtnYes);
    CHECK(value() == (onOff ? 1 : pv.min + 1));
  }
}

TEST_CASE("AC-P4.1 every R-P4 source makes EDITED; operator mask changes do not", "[panel]") {
  struct Src {
    const char* what;
    std::function<void(Rig&)> act;
  };
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  raw[kFeedback] = 6;
  const std::vector<uint8_t> voice(raw, raw + 155);
  const std::vector<Src> sources = {
      {"front panel", [](Rig& r) { toEdit(r); r.press(kBtnYes); }},
      {"programmer", [](Rig& r) { r.prog(kLfoDelay, 9); }},
      {"host automation", [](Rig& r) { r.host(kLfoDelay, 9); }},
      {"SysEx parameter change", [](Rig& r) {
         r.global(Global::SysInfo, 1);
         r.midi({0xF0, 0x43, 0x10, 0x01, 138 - 128, 9, 0xF7});
       }},
      {"received single voice", [voice](Rig& r) {
         r.global(Global::SysInfo, 1);
         uint8_t m[163];
         sysex::buildVoiceDump(voice.data(), 0, m);
         r.midi(std::vector<uint8_t>(m, m + 163));
       }},
      {"file-loaded voice", [voice](Rig& r) {
         r.postPayload(cmd(CmdType::LoadVoice, Source::BankManager), voice.data(), 155);
         r.render(64);
       }},
      {"VOICE INIT", [](Rig& r) { r.presses({kBtnFunction, 10, kBtnYes, kBtnYes}); }},
      {"EDIT RECALL", [](Rig& r) { r.presses({kBtnFunction, 9, kBtnYes, kBtnYes}); }},
      {"EG copy", [](Rig& r) { r.presses({kBtnEdit, kBtnStore, 2}); }},
      {"randomise", [](Rig& r) { r.simple(CmdType::Randomise, Source::Programmer, 1); }},
  };
  for (const auto& s : sources) {
    INFO(s.what);
    Rig r;
    r.press(4);  // CLEAN on BASS (EG copy OP1 -> OP2 changes bytes)
    REQUIRE_FALSE(r.edited());
    s.act(r);
    CHECK(r.edited());
    CHECK(r.ledDot == 1);
  }
  // Mask toggles from every path leave CLEAN.
  Rig r;
  r.press(4);
  toEdit(r);
  r.press(3);
  r.prog(kOpMask, 0x2A);
  r.host(kOpMask, 0x15);
  r.global(Global::SysInfo, 1);
  r.midi({0xF0, 0x43, 0x10, 0x01, 155 - 128, 0x07, 0xF7});
  CHECK(r.core().opMask == 0x07);
  CHECK_FALSE(r.edited());
}

TEST_CASE("AC-P6.1 program change then EDIT RECALL brings the edits back", "[panel]") {
  Rig r;
  r.press(6);
  r.host(opParam(3, kOpCoarse), 7);
  r.host(kName, 'Z');
  const auto edits = rawVec(r.ps().edit);
  r.midi(pc(1, 1));  // INT 2
  CHECK(r.ps().srcSlot == 2);
  CHECK_FALSE(r.edited());
  r.presses({kBtnFunction, 9, kBtnYes, kBtnYes});
  CHECK(rawVec(r.ps().edit) == edits);
  CHECK(r.edited());
}

TEST_CASE("AC-P7.1 every voice load resets the operator mask to all on", "[panel]") {
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  const std::vector<std::function<void(Rig&)>> loads = {
      [](Rig& r) { r.presses({kBtnMemInt, 5}); },
      [](Rig& r) { r.midi(pc(1, 7)); },
      [](Rig& r) { r.simple(CmdType::SelectVoice, Source::BankManager, kBankInt, 3); },
      [](Rig& r) { r.simple(CmdType::ProgramChange, Source::Host, 9); },
      [raw](Rig& r) {
        r.global(Global::SysInfo, 1);
        uint8_t m[163];
        sysex::buildVoiceDump(raw, 0, m);
        r.midi(std::vector<uint8_t>(m, m + 163));
      },
      [raw](Rig& r) {
        r.postPayload(cmd(CmdType::LoadVoice, Source::BankManager), raw, 155);
        r.render(64);
      },
      [](Rig& r) { r.presses({kBtnFunction, 10, kBtnYes, kBtnYes}); },
      [](Rig& r) { r.presses({kBtnFunction, 9, kBtnYes, kBtnYes}); },
      [](Rig& r) { r.simple(CmdType::InitVoice); },
  };
  int i = 0;
  for (const auto& load : loads) {
    INFO("load " << i++);
    Rig r;
    toEdit(r);
    r.press(3);  // OP3 off
    REQUIRE(r.core().opMask == 0x37);
    load(r);
    CHECK(r.core().opMask == 0x3F);
  }
}

TEST_CASE("AC-P9.1 memory protect blocks every write path; unprotected they succeed", "[panel]") {
  // A cartridge that differs from INIT VOICE and from the internal bank (slot 5 renamed).
  std::vector<uint8_t> other(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
  std::memcpy(other.data() + 4 * 128 + 118, "OTHER     ", 10);
  auto prepared = [other](bool prot) {
    auto r = std::make_unique<Rig>();
    std::vector<uint8_t> p = other;
    p.push_back(0);
    r->postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
    r->render(64);
    r->global(Global::ProtectInt, prot);
    r->global(Global::ProtectCrt, prot);
    r->press(5);
    r->host(kFeedback, 2);
    return r;
  };
  struct Path {
    const char* what;
    int bank;
    std::function<void(Rig&)> act;
  };
  const std::vector<Path> paths = {
      {"STORE to INT", kBankInt, [](Rig& r) { r.presses({kBtnStore, 12}); }},
      {"CARTRIDGE LOAD", kBankInt, [](Rig& r) { r.presses({kBtnFunction, 16, kBtnYes, kBtnYes}); }},
      {"bank-manager drop into INT", kBankInt,
       [](Rig& r) {
         Command c = cmd(CmdType::CopyVoice, Source::BankManager, kBankInt, 5, kBankInt);
         c.data[0] = 12;
         r.post(c);
         r.render(64);
       }},
      {"bank-manager load into INT", kBankInt,
       [other](Rig& r) {
         r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankInt), other.data(), 4096);
         r.render(64);
       }},
      {"MIDI bank receive", kBankInt,
       [other](Rig& r) {
         r.global(Global::SysInfo, 1);
         uint8_t m[4104];
         sysex::buildBankDump(other.data(), 0, m);
         r.midi(std::vector<uint8_t>(m, m + 4104));
       }},
      {"STORE to CRT", kBankCrt, [](Rig& r) { r.presses({kBtnMemCrt, kBtnStore, 12}); }},
      {"CARTRIDGE SAVE", kBankCrt, [](Rig& r) { r.presses({kBtnFunction, 15, kBtnYes, kBtnYes}); }},
      {"CARTRIDGE FORMAT", kBankCrt, [](Rig& r) { r.presses({kBtnFunction, 11, kBtnYes, kBtnYes}); }},
      {"bank-manager drop into CRT", kBankCrt,
       [](Rig& r) {
         Command c = cmd(CmdType::CopyVoice, Source::BankManager, kBankInt, 5, kBankCrt);
         c.data[0] = 12;
         r.post(c);
         r.render(64);
       }},
  };
  for (const auto& p : paths) {
    for (bool prot : {true, false}) {
      INFO(p.what << (prot ? " (protected)" : " (unprotected)"));
      auto r = prepared(prot);
      const std::vector<uint8_t> before = p.bank == kBankInt ? std::vector<uint8_t>(r->ps().intBank, r->ps().intBank + 4096)
                                                             : std::vector<uint8_t>(r->ps().crtBank, r->ps().crtBank + 4096);
      r->log.clear();
      p.act(*r);
      const uint8_t* now = p.bank == kBankInt ? r->ps().intBank : r->ps().crtBank;
      if (prot) {
        CHECK(std::memcmp(now, before.data(), 4096) == 0);
        bool shown = false;
        for (const auto& x : r->log)
          if (x.e.type == EvType::Lcd && std::string(x.e.text + 16, 16) == "MEMORY PROTECTED") shown = true;
        CHECK(shown);
      } else {
        CHECK(std::memcmp(now, before.data(), 4096) != 0);
      }
    }
  }
  // The edit buffer is never protected; inserting or ejecting a cartridge is not a write.
  auto r = prepared(true);
  r->prog(kFeedback, 7);
  CHECK(r->raw(kFeedback) == 7);
  r->simple(CmdType::EjectCartridge, Source::BankManager);
  CHECK(r->ps().crtPresent == 0);
  r->newCartridge();
  CHECK(r->ps().crtPresent == 1);
}

TEST_CASE("AC-P11.1 COMPARE plays the voice as selected, after its slot was overwritten", "[panel]") {
  Rig r;
  r.global(Global::ProtectInt, 0);
  r.press(5);
  const sysex::Voice original = r.ps().stored;
  std::vector<uint8_t> other(4096, 0);
  for (int s = 0; s < 32; ++s) {
    sysex::Voice v;
    sysex::initVoice(v);
    sysex::packVoice(v, other.data() + s * 128);
  }
  r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankInt), other.data(), 4096);
  r.render(64);
  REQUIRE(std::memcmp(r.ps().intBank + 4 * 128, other.data() + 4 * 128, 128) == 0);
  r.host(kFeedback, 3);
  toEdit(r);
  r.press(kBtnEdit);
  REQUIRE(r.core().comparing);
  CHECK(sounding(r) == effOf(original, 0x3F));
  CHECK(r.l2 == "INT 5 BRASS     ");
}

TEST_CASE("AC-S2.1 host automation on EDIT page 8 keeps mode, page and operator", "[panel]") {
  Rig r;
  toEdit(r);
  r.press(8);
  r.log.clear();
  r.host(opParam(2, kOpLevel), 70);
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 8);
  CHECK(r.core().selOp == 1);
  CHECK(r.edited());
  CHECK(r.l2 == "FEEDBACK     = 0");
  // A pending dialog stays (SPEC §8); CC5 is an external change too.
  r.press(kBtnProtInt);
  r.host(opParam(2, kOpLevel), 71);
  CHECK(r.core().dialog == Dialog::ProtInt);
  r.midi(cc(1, 5, 64));
  CHECK(r.fnv(kFnPortaTime) == 50);
  CHECK(r.core().dialog == Dialog::ProtInt);
  CHECK(r.panelMode == kModeEdit);
}

// ============================================================================
// AC-U2.1 / AC-U3.1 (core side of the operator and voice menus)
// ============================================================================

TEST_CASE("AC-U2.1 operator paste, swap, EG copy and init operator", "[panel]") {
  Rig r;
  r.press(2);  // E.PIANO: all operators differ
  const auto v0 = rawVec(r.ps().edit);
  // Copy OP4 (UI side: its 21 bytes), paste into OP1.
  Command paste = cmd(CmdType::WriteOperator, Source::Programmer, 1);
  for (int k = 0; k < 21; ++k) paste.data[k] = v0[size_t(opParam(4, k))];
  r.post(paste);
  r.render(64);
  for (int k = 0; k < 21; ++k) CHECK(r.raw(opParam(1, k)) == v0[size_t(opParam(4, k))]);
  CHECK(r.edited());
  // Swap OP2 <-> OP6.
  auto v1 = rawVec(r.ps().edit);
  r.simple(CmdType::SwapOperators, Source::Programmer, 2, 6);
  for (int k = 0; k < 21; ++k) {
    CHECK(r.raw(opParam(2, k)) == v1[size_t(opParam(6, k))]);
    CHECK(r.raw(opParam(6, k)) == v1[size_t(opParam(2, k))]);
  }
  // EG copy OP3 -> OP5.
  v1 = rawVec(r.ps().edit);
  r.simple(CmdType::EgCopy, Source::Programmer, 3, 5);
  for (int k = 0; k < 21; ++k)
    CHECK(r.raw(opParam(5, k)) == (k < 8 ? v1[size_t(opParam(3, k))] : v1[size_t(opParam(5, k))]));
  // Init operator: R-P10 operator values with output level 0.
  Command init = cmd(CmdType::WriteOperator, Source::Programmer, 3);
  sysex::initOperatorRaw(init.data, 0);
  const uint8_t want[21] = {99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7};
  CHECK(std::memcmp(init.data, want, 21) == 0);
  r.post(init);
  r.render(64);
  for (int k = 0; k < 21; ++k) CHECK(r.raw(opParam(3, k)) == want[k]);
  // Other bytes untouched throughout.
  for (int i = 126; i < 155; ++i) CHECK(r.raw(i) == v0[size_t(i)]);
}

TEST_CASE("AC-U3.1 100 randomisations (seed 1) stay in the R-U3 ranges", "[panel]") {
  Rig r;
  r.press(8);  // STRINGS: non-default globals and name
  const auto v0 = rawVec(r.ps().edit);
  for (int n = 0; n < 100; ++n) {
    INFO("randomisation " << n);
    toEdit(r);
    r.press(2);  // OP2 masked off
    r.simple(CmdType::Randomise, Source::Programmer, n == 0 ? 1 : 0);
    const uint8_t* v = r.ps().edit.raw;
    CHECK(v[kAlgorithm] <= 31);
    CHECK(v[kFeedback] <= 7);
    const uint8_t carriers = sysex::carrierMask(v[kAlgorithm]);
    for (int op = 1; op <= 6; ++op) {
      const uint8_t* o = v + opBase(op);
      CHECK(o[kOpMode] == 0);
      CHECK(o[kOpFine] == 0);
      CHECK((o[kOpDetune] >= 4 && o[kOpDetune] <= 10));
      CHECK(o[kOpCoarse] <= 8);
      if (carriers & (1 << (op - 1))) CHECK((o[kOpLevel] >= 60 && o[kOpLevel] <= 99));
      else CHECK(o[kOpLevel] <= 90);
      for (int k = 0; k < 4; ++k) CHECK((o[kOpR1 + k] >= 30 && o[kOpR1 + k] <= 99));
      CHECK(o[kOpL1] == 99);
      CHECK((o[kOpL2] >= 50 && o[kOpL2] <= 99));
      CHECK(o[kOpL3] <= 99);
      CHECK(o[kOpL4] == 0);
      CHECK(o[kOpKvs] <= 3);
      CHECK(o[kOpBreakPoint] == 0);
      CHECK(o[kOpLeftDepth] == 0);
      CHECK(o[kOpRightDepth] == 0);
      CHECK(o[kOpLeftCurve] == 0);
      CHECK(o[kOpRightCurve] == 0);
      CHECK(o[kOpRateScaling] == 0);
      CHECK(o[kOpAms] == 0);
    }
    for (int i = 126; i < 155; ++i)
      if (i != kAlgorithm && i != kFeedback) CHECK(v[i] == v0[size_t(i)]);
    CHECK(r.core().opMask == 0x3F);
    CHECK(r.edited());
  }
  // The coarse weights: 1 and 2 three times as likely as the others.
  int hist[9] = {};
  Rig h;
  h.simple(CmdType::Randomise, Source::Programmer, 1);
  for (int n = 0; n < 300; ++n) {
    for (int op = 1; op <= 6; ++op) ++hist[h.raw(opParam(op, kOpCoarse))];
    h.simple(CmdType::Randomise, Source::Programmer, 0);
  }
  CHECK(hist[1] > 2 * hist[5]);
  CHECK(hist[2] > 2 * hist[7]);
  // Same seed, same result.
  Rig a, b;
  a.simple(CmdType::Randomise, Source::Programmer, 1);
  b.simple(CmdType::Randomise, Source::Programmer, 1);
  CHECK(rawVec(a.ps().edit) == rawVec(b.ps().edit));
}

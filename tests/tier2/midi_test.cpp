// MIDI receive / transmit: AC-M1.1 (core part), AC-M1.2, AC-M1.3, AC-M2.1, AC-M3.1,
// AC-F13.1, AC-F14.1.
#include "rig.h"
#include "sysex/default_bank.h"
#include "sysex/sysex.h"

using namespace rig;
using namespace vdx;
using namespace vdx::params;

namespace {

std::vector<uint8_t> voiceDump(int device, int algorithm, bool goodSum = true) {
  uint8_t raw[155];
  sysex::initVoiceRaw(raw);
  raw[kAlgorithm] = uint8_t(algorithm);
  uint8_t m[163];
  sysex::buildVoiceDump(raw, device, m);
  if (!goodSum) m[161] ^= 1;
  return std::vector<uint8_t>(m, m + 163);
}

std::vector<uint8_t> bankDump(int device, const std::vector<uint8_t>& packed, bool goodSum = true) {
  uint8_t m[4104];
  sysex::buildBankDump(packed.data(), device, m);
  if (!goodSum) m[4102] ^= 1;
  return std::vector<uint8_t>(m, m + 4104);
}

std::vector<uint8_t> otherBank() {
  std::vector<uint8_t> b(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
  std::memcpy(b.data() + 118, "RECEIVED  ", 10);
  return b;
}

std::vector<uint8_t> paramChange(int device, int group, int param, int value) {
  uint8_t m[7];
  sysex::buildParamChange(device, group, param, value, m);
  return std::vector<uint8_t>(m, m + 7);
}

int paramMessages(const std::vector<std::vector<uint8_t>>& out) {
  int n = 0;
  for (const auto& m : out)
    if (m.size() == 7 && m[0] == 0xF0 && (m[2] & 0xF0) == 0x10) ++n;
  return n;
}

}  // namespace

TEST_CASE("AC-M1.1 channel filter: receive channel 3, omni off / on", "[midi]") {
  Rig r;
  r.global(Global::MidiChannel, 3);
  r.global(Global::Omni, 0);
  const uint32_t n0 = r.core().noteOnsToEngine;
  r.midi(noteOn(1, 60, 100));
  CHECK(r.core().noteOnsToEngine == n0);
  r.midi(noteOn(3, 62, 100));
  CHECK(r.core().noteOnsToEngine == n0 + 1);
  CHECK(r.core().lastEngineNote == 62);
  // Every channel message obeys the filter (here: a controller and a program change).
  r.midi(cc(1, 7, 10));
  CHECK(r.globalv(Global::Volume) == 127);
  r.midi(pc(2, 4));
  CHECK(r.ps().srcSlot == 1);
  r.midi(pc(3, 4));
  CHECK(r.ps().srcSlot == 5);
  r.global(Global::Omni, 1);
  r.midi(noteOn(1, 64, 100));
  r.midi(noteOn(3, 65, 100));
  CHECK(r.core().noteOnsToEngine == n0 + 3);
}

TEST_CASE("AC-M1.2 each R-M1 receive row has its effect", "[midi]") {
  Rig r;
  auto& c = r.core();
  // Note on/off, velocity 0 = off, full velocity range.
  r.midi(noteOn(1, 60, 1));
  CHECK(c.lastEngineVelocity == 1);
  r.midi(noteOn(1, 61, 127));
  CHECK(c.lastEngineVelocity == 127);
  const uint32_t offs = c.noteOffsToEngine;
  r.midi(noteOn(1, 61, 0));
  CHECK(c.noteOffsToEngine == offs + 1);
  r.midi(noteOff(1, 60));
  CHECK(c.noteOffsToEngine == offs + 2);
  // Running status note-ons.
  r.midi({0x90, 40, 90, 41, 90, 42, 0});
  CHECK(c.lastEngineNote == 41);
  CHECK(c.noteOffsToEngine == offs + 3);
  // Channel aftertouch.
  r.midi({0xD0, 77});
  CHECK(c.aftertouchV == 77);
  // Pitch bend, 14 bit; the UI wheel follows.
  r.log.clear();
  r.midi({0xE0, 0x7F, 0x7F});
  CHECK(c.bend == 16383);
  const Ev* w = r.last(EvType::Wheels);
  REQUIRE(w != nullptr);
  CHECK(w->e.a == 16383);
  // CC1 / CC2 / CC4.
  r.midi(cc(1, 1, 90));
  CHECK(c.modWheel == 90);
  w = r.last(EvType::Wheels);
  REQUIRE(w != nullptr);
  CHECK(w->e.b == 90);
  r.midi(cc(1, 2, 33));
  CHECK(c.breathV == 33);
  r.midi(cc(1, 4, 44));
  CHECK(c.footV == 44);
  // CC5 -> function 69 = round(v x 99/127), as an external change.
  r.log.clear();
  r.midi(cc(1, 5, 64));
  CHECK(r.fnv(kFnPortaTime) == 50);
  const Ev* fe = r.last(EvType::FunctionParam);
  REQUIRE(fe != nullptr);
  CHECK(fe->e.src == Source::Midi);
  CHECK(r.panelMode == kModePlay);
  r.midi(cc(1, 5, 127));
  CHECK(r.fnv(kFnPortaTime) == 99);
  // CC65 portamento switch (defaults on), CC64 sustain.
  CHECK(c.portaOn);
  r.midi(cc(1, 65, 0));
  CHECK_FALSE(c.portaOn);
  r.midi(cc(1, 65, 64));
  CHECK(c.portaOn);
  r.midi(cc(1, 64, 64));
  CHECK(c.sustainOn);
  r.midi(cc(1, 64, 63));
  CHECK_FALSE(c.sustainOn);
  // CC6 / CC96 / CC97 act as the slider / YES / NO.
  r.press(kBtnEdit);
  r.press(10);
  r.midi(cc(1, 6, 127));
  CHECK(r.raw(kLfoSpeed) == 99);
  r.midi(cc(1, 6, 0));
  CHECK(r.raw(kLfoSpeed) == 0);
  r.midi(cc(1, 96, 127));
  CHECK(r.raw(kLfoSpeed) == 1);
  r.midi(cc(1, 97, 127));
  CHECK(r.raw(kLfoSpeed) == 0);
  // CC7 -> master volume and the volume slider.
  r.log.clear();
  r.midi(cc(1, 7, 64));
  CHECK(r.globalv(Global::Volume) == 64);
  CHECK(r.inst.core().masterVolume() == 64);
  const Ev* g = r.last(EvType::GlobalValue);
  REQUIRE(g != nullptr);
  CHECK(g->e.a == int(Global::Volume));
  CHECK(g->e.b == 64);
  // CC120 / CC123.
  r.midi(cc(1, 120, 0));
  CHECK(c.allSoundOffs == 1);
  r.midi(cc(1, 123, 0));
  CHECK(c.allNotesOffs == 1);
  // Program change (F-14) and SysEx (F-13) are covered by AC-M1.3 / AC-F13.1.
  r.midi(pc(1, 2));
  CHECK(r.ps().srcSlot == 3);
}

TEST_CASE("AC-M1.3 program changes 0, 31, 32, 63, 64 with and without a cartridge", "[midi]") {
  for (bool crt : {false, true}) {
    INFO("cartridge " << crt);
    Rig r;
    if (crt) {
      std::vector<uint8_t> p(sysex::defaultBankPacked(), sysex::defaultBankPacked() + 4096);
      p.push_back(0);
      r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
      r.render(64);
    }
    r.midi(pc(1, 0));
    CHECK(r.ps().srcBank == kBankInt);
    CHECK(r.ps().srcSlot == 1);
    r.midi(pc(1, 31));
    CHECK(r.ps().srcBank == kBankInt);
    CHECK(r.ps().srcSlot == 32);
    r.midi(pc(1, 32));
    if (crt) {
      CHECK(r.ps().srcBank == kBankCrt);
      CHECK(r.ps().srcSlot == 1);
      CHECK(r.ps().selMem == kBankCrt);
    } else {
      CHECK(r.ps().srcBank == kBankInt);
      CHECK(r.ps().srcSlot == 32);
    }
    r.midi(pc(1, 63));
    if (crt) {
      CHECK(r.ps().srcBank == kBankCrt);
      CHECK(r.ps().srcSlot == 32);
    } else {
      CHECK(r.ps().srcSlot == 32);
    }
    const auto before = r.ps();
    r.midi(pc(1, 64));
    CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
    r.midi(pc(1, 127));
    CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
  }
}

TEST_CASE("AC-M2.1 stream robustness: 1-byte chunks with F8, oversized SysEx, running status", "[midi]") {
  Rig r;
  r.global(Global::SysInfo, 1);
  r.global(Global::ProtectInt, 0);
  const auto bank = bankDump(0, otherBank());
  for (size_t i = 0; i < bank.size(); ++i) {
    r.inst.midi(&bank[i], 1, 0);
    if (i % 97 == 0) {
      const uint8_t clock = 0xF8;
      r.inst.midi(&clock, 1, 0);
    }
    if (i % 512 == 0) r.render(64);
  }
  r.render(64);
  CHECK(std::memcmp(r.ps().intBank, otherBank().data(), 4096) == 0);
  CHECK(r.l2 == " MIDI RECEIVED  ");
  // A 5000-byte SysEx is ignored (here: a bank header with too much data).
  std::vector<uint8_t> big = bankDump(0, std::vector<uint8_t>(4096, 0));
  big.insert(big.end() - 2, 896, 0x01);
  REQUIRE(big.size() == 5000);
  r.midi(big);
  CHECK(std::memcmp(r.ps().intBank, otherBank().data(), 4096) == 0);
  // Running-status notes after it all play.
  const uint32_t n0 = r.core().noteOnsToEngine;
  r.midi({0x90, 60, 100, 62, 100, 64, 100, 0xF8, 65, 100});
  CHECK(r.core().noteOnsToEngine == n0 + 4);
}

TEST_CASE("AC-M3.1 transmit while SYS INFO AVAIL: panel 1, programmer 1, drag <= 101, host 0, received 0",
          "[midi]") {
  for (bool avail : {true, false}) {
    INFO("AVAIL " << avail);
    Rig r;
    r.global(Global::SysInfo, avail);
    r.takeMidiOut();
    // Front-panel edit: one message.
    r.press(kBtnEdit);
    r.press(8);
    r.press(kBtnYes);
    r.ms(20);
    auto out = r.takeMidiOut();
    CHECK(paramMessages(out) == (avail ? 1 : 0));
    if (avail) {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == paramChange(0, 0, kFeedback, 1));  // transmit channel 1
    }
    // Programmer edit: one message.
    r.prog(opParam(2, kOpLevel), 40);
    r.ms(20);
    out = r.takeMidiOut();
    CHECK(paramMessages(out) == (avail ? 1 : 0));
    if (avail) CHECK(out.back() == paramChange(0, 0, opParam(2, kOpLevel), 40));
    // A 1 s programmer drag: at most 101 messages, ending in the final value.
    int v = 0;
    const int blocks = int(kInternalRate / 64);
    for (int b = 0; b < blocks; ++b) {
      v = (b * 99) / (blocks - 1);
      r.post(cmd(CmdType::SetVoiceParam, Source::Programmer, kLfoSpeed, v));
      r.render(64);
    }
    r.ms(50);
    out = r.takeMidiOut();
    const int n = paramMessages(out);
    if (avail) {
      CHECK(n <= 101);
      CHECK(n >= 50);
      REQUIRE(!out.empty());
      CHECK(out.back() == paramChange(0, 0, kLfoSpeed, 99));
    } else {
      CHECK(n == 0);
    }
    // A function parameter edit is sent in group 2.
    r.progFn(kFnPortaTime, 12);
    r.ms(20);
    out = r.takeMidiOut();
    if (avail) {
      REQUIRE(out.size() == 1);
      CHECK(out[0] == paramChange(0, 2, kFnPortaTime, 12));
    } else {
      CHECK(out.empty());
    }
    // Host automation: none.
    r.host(kLfoDelay, 30);
    r.hostFn(kFnBendRange, 7);
    r.ms(20);
    CHECK(paramMessages(r.takeMidiOut()) == 0);
    // A received parameter change: none (and it applies only while AVAIL).
    r.midi(paramChange(0, 0, kLfoDelay, 31));
    r.ms(20);
    CHECK(paramMessages(r.takeMidiOut()) == 0);
    CHECK(r.raw(kLfoDelay) == (avail ? 31 : 30));
    // Received MIDI (CC5, CC6) is never transmitted.
    r.midi(cc(1, 5, 10));
    r.midi(cc(1, 6, 10));
    r.ms(20);
    CHECK(paramMessages(r.takeMidiOut()) == 0);
  }
}

TEST_CASE("AC-F13.1 SysEx receive filter, messages and replies", "[midi]") {
  struct Cfg {
    bool avail, omni;
    int channel;  // receive channel
    int device;   // nibble in the message
    bool protInt;
  };
  std::vector<Cfg> cfgs;
  for (bool avail : {true, false})
    for (bool omni : {true, false})
      for (int dev : {2, 5})
        for (bool prot : {true, false}) cfgs.push_back({avail, omni, 3, dev, prot});
  for (const auto& c : cfgs) {
    INFO("avail " << c.avail << " omni " << c.omni << " device " << c.device << " protect " << c.protInt);
    const bool accepted = c.avail && (c.omni || c.device == c.channel - 1);
    // Single voice.
    {
      Rig r;
      r.global(Global::SysInfo, c.avail);
      r.global(Global::Omni, c.omni);
      r.global(Global::MidiChannel, c.channel);
      r.global(Global::ProtectInt, c.protInt);
      r.press(3);
      r.host(kFeedback, 5);  // EDITED: recall capture
      const auto edits = std::vector<uint8_t>(r.ps().edit.raw, r.ps().edit.raw + 155);
      r.press(kBtnEdit);
      r.press(3);  // OP3 off
      r.midi(voiceDump(c.device, 21));
      if (accepted) {
        CHECK(r.raw(kAlgorithm) == 21);
        CHECK(r.edited());
        CHECK(r.core().opMask == 0x3F);
        CHECK(std::vector<uint8_t>(r.ps().recall.raw, r.ps().recall.raw + 155) == edits);
        CHECK(r.l2 == " MIDI RECEIVED  ");
        CHECK(r.l1 == "ALG22 111111    ");
        r.ms(2100);
        CHECK(r.l2 == "ALGORITHM SELECT");  // then the current page re-renders
      } else {
        CHECK(r.raw(kAlgorithm) != 21);
        CHECK(r.core().msg == Msg::None);
      }
    }
    // Bank.
    {
      Rig r;
      r.global(Global::SysInfo, c.avail);
      r.global(Global::Omni, c.omni);
      r.global(Global::MidiChannel, c.channel);
      r.global(Global::ProtectInt, c.protInt);
      r.press(4);
      const auto edit = std::vector<uint8_t>(r.ps().edit.raw, r.ps().edit.raw + 155);
      r.midi(bankDump(c.device, otherBank()));
      const bool loaded = std::memcmp(r.ps().intBank, otherBank().data(), 4096) == 0;
      if (accepted && !c.protInt) {
        CHECK(loaded);
        CHECK(r.l2 == " MIDI RECEIVED  ");
      } else if (accepted) {
        CHECK_FALSE(loaded);
        CHECK(r.l2 == "MEMORY PROTECTED");
      } else {
        CHECK_FALSE(loaded);
        CHECK(r.core().msg == Msg::None);
      }
      CHECK(std::vector<uint8_t>(r.ps().edit.raw, r.ps().edit.raw + 155) == edit);  // edit buffer unchanged
    }
    // Parameter change: applied like host automation, no message.
    {
      Rig r;
      r.global(Global::SysInfo, c.avail);
      r.global(Global::Omni, c.omni);
      r.global(Global::MidiChannel, c.channel);
      r.press(kBtnEdit);
      r.press(8);
      r.log.clear();
      r.midi(paramChange(c.device, 0, opParam(4, kOpLevel), 66));
      r.midi(paramChange(c.device, 0, kOpMask, 0x3E));
      r.midi(paramChange(c.device, 2, kFnBendRange, 9));
      if (accepted) {
        CHECK(r.raw(opParam(4, kOpLevel)) == 66);
        CHECK(r.core().opMask == 0x3E);
        CHECK(r.fnv(kFnBendRange) == 9);
        CHECK(r.panelPage == 8);
        CHECK(r.panelMode == kModeEdit);
        const Ev* e = r.last(EvType::VoiceParam);
        REQUIRE(e != nullptr);
        CHECK(e->e.src == Source::SysEx);
      } else {
        CHECK(r.raw(opParam(4, kOpLevel)) == 0);
        CHECK(r.fnv(kFnBendRange) == 2);
      }
      CHECK(r.core().msg == Msg::None);
      // Numbers outside 0-155 / 64-77 are ignored.
      const auto before = r.ps();
      r.midi(paramChange(c.device, 0, 156, 1));
      r.midi(paramChange(c.device, 2, 63, 1));
      r.midi(paramChange(c.device, 2, 78, 1));
      r.midi(paramChange(c.device, 1, 10, 1));
      CHECK(std::memcmp(&before, &r.ps(), sizeof before) == 0);
    }
    // Dump requests: answered on channel 1 only while accepted.
    {
      Rig r;
      r.global(Global::SysInfo, c.avail);
      r.global(Global::Omni, c.omni);
      r.global(Global::MidiChannel, c.channel);
      r.press(6);
      r.takeMidiOut();
      r.midi({0xF0, 0x43, uint8_t(0x20 | c.device), 0x00, 0xF7});
      r.midi({0xF0, 0x43, uint8_t(0x20 | c.device), 0x09, 0xF7});
      const auto out = r.takeMidiOut();
      if (accepted) {
        REQUIRE(out.size() == 2);
        uint8_t v[163];
        sysex::buildVoiceDump(r.ps().edit.raw, 0, v);
        CHECK(out[0] == std::vector<uint8_t>(v, v + 163));
        CHECK(out[1] == bankDump(0, std::vector<uint8_t>(r.ps().intBank, r.ps().intBank + 4096)));
        CHECK((out[0][2] & 0x0F) == 0);
      } else {
        CHECK(out.empty());
      }
    }
  }
  // Checksum mismatch: loads anyway, CHECK SUM ERROR!; other manufacturers / lengths ignored.
  Rig r;
  r.global(Global::SysInfo, 1);
  r.global(Global::ProtectInt, 0);
  r.midi(voiceDump(0, 12, false));
  CHECK(r.raw(kAlgorithm) == 12);
  CHECK(r.l2 == "CHECK SUM ERROR!");
  r.midi(bankDump(0, otherBank(), false));
  CHECK(std::memcmp(r.ps().intBank, otherBank().data(), 4096) == 0);
  CHECK(r.l2 == "CHECK SUM ERROR!");
  auto foreign = voiceDump(0, 13);
  foreign[1] = 0x41;
  r.midi(foreign);
  auto shortV = voiceDump(0, 14);
  shortV.erase(shortV.begin() + 10);
  r.midi(shortV);
  CHECK(r.raw(kAlgorithm) == 12);
  // A dump cancels a pending dialog.
  r.press(kBtnStore);
  REQUIRE(r.core().dialog == Dialog::Store);
  r.midi(voiceDump(0, 15));
  CHECK(r.core().dialog == Dialog::None);
  // Request dump / send bank from the bank manager work regardless of AVAIL.
  r.global(Global::SysInfo, 0);
  r.takeMidiOut();
  r.simple(CmdType::RequestDump, Source::BankManager);
  r.simple(CmdType::SendBank, Source::BankManager);
  const auto out = r.takeMidiOut();
  REQUIRE(out.size() == 2);
  CHECK(out[0] == std::vector<uint8_t>({0xF0, 0x43, 0x20, 0x09, 0xF7}));
  CHECK(out[1].size() == 4104);
}

TEST_CASE("AC-F14.1 program change 33 cancels a pending dialog and loads CRT 2", "[midi]") {
  Rig r;
  std::vector<uint8_t> p = otherBank();
  std::memcpy(p.data() + 128 + 118, "CART TWO  ", 10);
  p.push_back(0);
  r.postPayload(cmd(CmdType::LoadBank, Source::BankManager, kBankCrt), p.data(), int(p.size()));
  r.render(64);
  r.press(kBtnEdit);
  r.press(kBtnStore);
  REQUIRE(r.core().dialog == Dialog::EgCopy);
  r.midi(pc(1, 33));
  CHECK(r.core().dialog == Dialog::None);
  CHECK(r.ps().srcBank == kBankCrt);
  CHECK(r.ps().srcSlot == 2);
  CHECK(r.ps().selMem == kBankCrt);
  CHECK(r.panelMode == kModeEdit);
  CHECK(std::string(reinterpret_cast<const char*>(r.ps().edit.raw + kName), 10) == "CART TWO  ");
  // Naming ends on a program change.
  r.press(32);
  REQUIRE(r.panelMode == kModeEditName);
  r.midi(pc(1, 4));
  CHECK(r.panelMode == kModeEdit);
  CHECK(r.panelPage == 32);
  // Host programs act as program changes.
  r.simple(CmdType::ProgramChange, Source::Host, 9);
  CHECK(r.ps().srcBank == kBankInt);
  CHECK(r.ps().srcSlot == 10);
}

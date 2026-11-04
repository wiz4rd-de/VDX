// LCD text (SPEC §5.3, R-P2), LED and lamps (§5.4).
#include <cstring>

#include "panel/core_impl.h"
#include "panel/format.h"

namespace vdx {

using namespace params;

namespace {

// One 16-character LCD line under construction.
struct Line {
  char* c;
  int n = 0;
  explicit Line(char* buf) : c(buf) {}
  Line& add(const char* s) {
    while (*s && n < 16) c[n++] = *s++;
    return *this;
  }
  Line& ch(char x) {
    if (n < 16) c[n++] = x;
    return *this;
  }
  Line& nn(int v) {
    char t[fmt::kMaxText];
    fmt::nn(v, t);
    return add(t);
  }
  Line& padTo(int col) {
    while (n < col && n < 16) c[n++] = ' ';
    return *this;
  }
  void finish() {
    padTo(16);
    c[16] = 0;
  }
};

void setLine(char* out, const char* s) {
  Line l(out);
  l.add(s);
  l.finish();
}

const char* const kCtlSrc[4] = {"WHEEL ", "FOOT  ", "BREATH", "AFTER "};
const char* const kCtlParam[4] = {"RANGE", "PITCH", "AMP  ", "EG B."};

}  // namespace

bool Core::Impl::showsOp() const {
  if (mode != kModeEdit) return false;
  const int p = editPage;
  return p == 16 || (p >= 18 && p <= 28) || (p == 17 && b17Sub == 0);
}

void Core::Impl::playLines(LcdState& o, bool compare) const {
  setLine(o.l1, ps.selMem == kBankCrt ? "CARTRIDGE VOICE" : "INTERNAL VOICE");
  Line l2(o.l2);
  l2.add(ps.srcBank == kBankCrt ? "CRT" : "INT").nn(ps.srcSlot).ch(' ');
  char name[11];
  sysex::displayName(compare ? ps.stored.raw : ps.edit.raw, name);
  name[10] = 0;
  l2.add(name).finish();
  if (!compare && ps.edited) {
    o.cursor = true;
    o.row = 1;
    o.col = 6;
  }
}

void Core::Impl::editL1(char* out) const {
  Line l(out);
  char digits[8];
  fmt::maskDigits(opMask, digits);
  l.add("ALG").nn(effective(kAlgorithm, ps.edit.raw[kAlgorithm]) + 1).ch(' ').add(digits).ch(' ');
  if (showsOp()) l.add("OP").ch(char('0' + selOp));
  l.finish();
}

void Core::Impl::editL2(char* out) const {
  uint8_t eff[156];
  editEffective(eff);
  const int op = selOp;
  char f[fmt::kMaxText];
  auto field = [&](int index) {
    fmt::voiceField(eff, index, eff[index], f);
    return f;
  };
  Line l(out);
  switch (editPage) {
    case 7: l.add("ALGORITHM SELECT"); break;
    case 8: l.add("FEEDBACK     =").add(field(kFeedback)); break;
    case 9: l.add("LFO WAVE=").add(field(kLfoWave)); break;
    case 10: l.add("LFO SPEED    =").add(field(kLfoSpeed)); break;
    case 11: l.add("LFO DELAY    =").add(field(kLfoDelay)); break;
    case 12: l.add("LFO PM DEPTH =").add(field(kLfoPmd)); break;
    case 13: l.add("LFO AM DEPTH =").add(field(kLfoAmd)); break;
    case 14: l.add("LFO KEY SYNC=").add(field(kLfoKeySync)); break;
    case 15: l.add("P MOD SENS.  =").add(field(kPitchModSens)); break;
    case 16: l.add("A MOD SENS.  =").add(field(opParam(op, kOpAms))); break;
    case 17:
      if (b17Sub == 0) l.add(field(opParam(op, kOpMode)));
      else l.add("OSC KEY SYNC=").add(field(kOscKeySync));
      break;
    case 18: l.add("F COARSE=").add(field(opParam(op, kOpCoarse))); break;
    case 19: l.add("F FINE  =").add(field(opParam(op, kOpFine))); break;
    case 20: l.add("OSC DETUNE   =").add(field(opParam(op, kOpDetune))); break;
    case 21: l.add("EG  RATE  ").ch(char('1' + egStage)).add("  =").add(field(opParam(op, kOpR1 + egStage))); break;
    case 22: l.add("EG  LEVEL ").ch(char('1' + egStage)).add("  =").add(field(opParam(op, kOpL1 + egStage))); break;
    case 23: l.add("BREAK POINT=").add(field(opParam(op, kOpBreakPoint))); break;
    case 24:
      l.add(scaleSide ? "R KEY SCALE=" : "L KEY SCALE=")
          .add(field(opParam(op, scaleSide ? kOpRightCurve : kOpLeftCurve)));
      break;
    case 25:
      l.add(scaleSide ? "R SCALE DEPTH=" : "L SCALE DEPTH=")
          .add(field(opParam(op, scaleSide ? kOpRightDepth : kOpLeftDepth)));
      break;
    case 26: l.add("RATE SCALING =").add(field(opParam(op, kOpRateScaling))); break;
    case 27: l.add("OUTPUT LEVEL =").add(field(opParam(op, kOpLevel))); break;
    case 28: l.add("KEY VELOCITY =").add(field(opParam(op, kOpKvs))); break;
    case 29: l.add("P EG RATE  ").ch(char('1' + pegStage)).add(" =").add(field(kPegR1 + pegStage)); break;
    case 30: l.add("P EG LEVEL ").ch(char('1' + pegStage)).add(" =").add(field(kPegL1 + pegStage)); break;
    case 31: l.add("MIDDLE C = ").add(field(kTranspose)); break;
    case 32: {
      char name[11];
      sysex::displayName(ps.edit.raw, name);
      name[10] = 0;
      l.add("NAME= ").add(name);
      break;
    }
    default: break;
  }
  l.finish();
}

void Core::Impl::fnLines(LcdState& o) const {
  const int p = fnPage;
  if (p == 15) setLine(o.l1, " SAVE MEMORY");
  else if (p == 16) setLine(o.l1, " LOAD MEMORY");
  else setLine(o.l1, "FUNCTION CONTROL");
  char f[fmt::kMaxText];
  auto field = [&](int n) {
    fmt::functionField(ps.fn, n, ps.fn[n - kFnFirst], f);
    return f;
  };
  Line l(o.l2);
  const bool sure = dialog == Dialog::Confirm;
  switch (p) {
    case 1: l.add("MASTER TUNE ADJ"); break;
    case 2: l.add(field(kFnMono)); break;
    case 3: l.add("P BEND RANGE=").add(field(kFnBendRange)); break;
    case 4: l.add("P BEND STEP=").add(field(kFnBendStep)); break;
    case 5: l.add(field(kFnPortaMode)); break;
    case 6: l.add("GLISSANDO=").add(field(kFnGlissando)); break;
    case 7: l.add("PORTA TIME=").add(field(kFnPortaTime)); break;
    case 8:
      if (b8Sub == 0) l.add("MIDI CH=").nn(ps.globals[int(Global::MidiChannel)]);
      else if (b8Sub == 1) l.add(ps.globals[int(Global::SysInfo)] ? "SYS INFO AVAIL" : "SYS INFO UNAVAIL");
      else l.add(" MIDI TRANSMIT ?");
      break;
    case 9: l.add(sure ? " ARE YOU SURE ? " : "EDIT RECALL ?"); break;
    case 10: l.add(sure ? " ARE YOU SURE ? " : "VOICE INIT ?"); break;
    case 11: l.add(sure ? " ARE YOU SURE ? " : "CARTRIDGE FORM ?"); break;
    case 14: l.add("BATTERY VOLT=3.0"); break;
    case 15:
    case 16: l.add(sure ? " ARE YOU SURE ? " : " ALL OF MEMORY ?"); break;
    default:
      if (p >= 17 && p <= 32) {
        const int group = (p - 17) / 4, kind = (p - 17) % 4;
        l.add(kCtlSrc[group]).ch(' ').add(kCtlParam[kind]).ch('=');
        const int range = ps.fn[kFnWheelRange + 2 * group - kFnFirst];
        const int assign = ps.fn[kFnWheelAssign + 2 * group - kFnFirst];
        if (kind == 0) l.nn(range);
        else {
          fmt::assignBitString(assign, kind - 1, f);
          l.add(f);
        }
      }
      break;
  }
  l.finish();
}

void Core::Impl::renderBase(LcdState& o) const {
  switch (dialog) {
    case Dialog::Store: {
      LcdState t{};
      playLines(t, false);
      std::memcpy(o.l1, t.l1, 17);
      setLine(o.l2, " MEMORY STORE");
      return;
    }
    case Dialog::EgCopy: {
      setLine(o.l1, " EG COPY");
      char t[17] = "from OPs to OP?";
      t[7] = char('0' + selOp);
      setLine(o.l2, t);
      return;
    }
    case Dialog::ProtInt:
    case Dialog::ProtCrt: {
      setLine(o.l1, " MEMORY PROTECT");
      const bool crt = dialog == Dialog::ProtCrt;
      Line l(o.l2);
      l.add(crt ? " CARTRIDGE" : " INTERNAL").padTo(13).add(fmt::onOff(ps.globals[crt ? int(Global::ProtectCrt) : int(Global::ProtectInt)]));
      l.finish();
      return;
    }
    default: break;
  }
  switch (mode) {
    case kModeEdit:
      if (comparing) {
        playLines(o, true);
        return;
      }
      editL1(o.l1);
      editL2(o.l2);
      return;
    case kModeEditName:
      editL1(o.l1);
      editL2(o.l2);
      o.cursor = true;
      o.row = 1;
      o.col = 6 + nameCursor;
      return;
    case kModeFunction: fnLines(o); return;
    default: playLines(o, false); return;
  }
}

void Core::Impl::renderLcd(LcdState& o) const {
  o.row = 0;
  o.col = 0;
  o.cursor = false;
  if (splash) {
    setLine(o.l1, "      VDX       ");
    setLine(o.l2, " FM SYNTHESIZER ");
    return;
  }
  const char* l2 = nullptr;
  switch (msg) {
    case Msg::None: renderBase(o); return;
    case Msg::NotReady:
      setLine(o.l1, "   NOT READY !  ");
      setLine(o.l2, "INSERT CARTRIDGE");
      return;
    case Msg::StateReset: {
      LcdState t{};
      playLines(t, false);
      std::memcpy(o.l1, t.l1, 17);
      setLine(o.l2, "  STATE RESET   ");
      return;
    }
    case Msg::Protected: l2 = "MEMORY PROTECTED"; break;
    case Msg::FormatConflict: l2 = "FORMAT CONFLICT!"; break;
    case Msg::ChecksumError: l2 = "CHECK SUM ERROR!"; break;
    case Msg::MidiReceived: l2 = " MIDI RECEIVED"; break;
    case Msg::Completed: l2 = " COMPLETED"; break;
  }
  LcdState base{};
  renderBase(base);  // L1 unchanged
  std::memcpy(o.l1, base.l1, 17);
  setLine(o.l2, l2);
}

void Core::Impl::lamps(int& num, int& modeMask, int& blink) const {
  num = modeMask = blink = 0;
  if (splash) return;
  auto bit = [](int btn) { return 1 << (btn - kBtnMemInt); };
  switch (mode) {
    case kModePlay:
      modeMask |= bit(ps.selMem == kBankCrt ? kBtnMemCrt : kBtnMemInt);
      if (ps.selMem == ps.srcBank && ps.srcSlot >= 1 && ps.srcSlot <= 32) num |= int(1u << (ps.srcSlot - 1));
      break;
    case kModeEdit:
    case kModeEditName:
      modeMask |= bit(kBtnEdit);
      num |= int(1u << (editPage - 1));
      if (comparing) blink |= bit(kBtnEdit);
      break;
    case kModeFunction:
      modeMask |= bit(kBtnFunction);
      num |= int(1u << (fnPage - 1));
      break;
    default: break;
  }
  if (dialog == Dialog::Store || dialog == Dialog::EgCopy) modeMask |= bit(kBtnStore);
  if (dialog == Dialog::ProtInt) modeMask |= bit(kBtnProtInt);
  if (dialog == Dialog::ProtCrt) modeMask |= bit(kBtnProtCrt);
}

}  // namespace vdx

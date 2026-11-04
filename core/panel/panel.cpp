// Front-panel state machine: SPEC §6 F-2 … F-11, F-14, §7.3 R-P1 … R-P11, §7.4 R-S2/R-S3.
#include <cmath>
#include <cstring>

#include "panel/core_impl.h"
#include "panel/format.h"
#include "sysex/sysex.h"

namespace vdx {

using namespace params;

namespace {

constexpr char kNameMap[33] = "1234567890ABCDEFGHIJKLMNOPQRSTUV";  // F-4.2, buttons 1..32

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isConfirmPage(int p) { return p == 9 || p == 10 || p == 11 || p == 15 || p == 16; }

}  // namespace

// ============================================================================
// Inputs
// ============================================================================

void Core::Impl::buttonDown(int id, bool allowHold) {
  if (id < 1 || id > kBtnYes) return;
  if (splash) return;  // F-1.4: front-panel inputs are ignored during the splash
  src = Source::Panel;
  tx = true;
  msg = Msg::None;  // F-2: any front-panel input dismisses a message, then acts
  if (dialog != Dialog::None) {
    // The dialog's own answers act on it; anything else cancels it, then acts.
    switch (dialog) {
      case Dialog::Store:
        if (id >= 1 && id <= 32) {
          dialog = Dialog::None;
          doStore(id);
          return;
        }
        if (id == kBtnStore || id == kBtnNo) {
          dialog = Dialog::None;
          return;
        }
        break;
      case Dialog::EgCopy:
        if (id >= 1 && id <= 6) {
          dialog = Dialog::None;
          doEgCopy(selOp, id);
          return;
        }
        if (id == kBtnStore || id == kBtnNo) {
          dialog = Dialog::None;
          return;
        }
        break;
      case Dialog::ProtInt:
      case Dialog::ProtCrt:
        if (id == kBtnYes || id == kBtnNo) {
          setGlobal(dialog == Dialog::ProtInt ? int(Global::ProtectInt) : int(Global::ProtectCrt), id == kBtnYes ? 1 : 0);
          return;
        }
        break;
      case Dialog::Confirm:
        if (id == kBtnYes) {
          dialog = Dialog::None;
          executeConfirm();
          return;
        }
        if (id == kBtnNo) {
          dialog = Dialog::None;  // back to the question
          return;
        }
        break;
      default: break;
    }
    dialog = Dialog::None;
  }

  if (id == kBtnYes || id == kBtnNo) {
    if (yesNo(id == kBtnYes) && allowHold) {
      held = id;
      nextRepeat = now + tRepeatDelay;
    }
    return;
  }

  if (mode == kModeEditName) {
    char c = 0;
    if (id <= 32) c = kNameMap[id - 1];
    else switch (id) {
        case kBtnStore: c = 'W'; break;
        case kBtnProtInt: c = 'X'; break;
        case kBtnProtCrt: c = 'Y'; break;
        case kBtnOpSelect: c = 'Z'; break;
        case kBtnEdit: c = ' '; break;
        case kBtnMemInt: c = '-'; break;
        case kBtnMemCrt: c = '.'; break;
        case kBtnFunction: endNaming(); return;  // → EDIT, VOICE NAME page, no cursor
        default: return;
      }
    writeVoice(kName + nameCursor, uint8_t(c));
    if (nameCursor < 9) ++nameCursor;
    return;
  }

  // ---- numbered buttons ----
  if (id <= 32) {
    switch (mode) {
      case kModePlay:
        if (ps.selMem == kBankCrt && !ps.crtPresent) {
          showMessage(Msg::NotReady);
          return;
        }
        selectVoice(ps.selMem, id, false);
        return;
      case kModeEdit:
        if (comparing) return;
        if (id <= 6) {
          writeVoice(kOpMask, opMask ^ (1 << (6 - id)));
          return;
        }
        if (id == 32) {
          showEditPage(32);
          mode = kModeEditName;
          nameCursor = 0;
          return;
        }
        showEditPage(id);
        return;
      case kModeFunction:
        if (id == 12 || id == 13) return;  // no effect at all
        if (id == fnPage && id == 8) {
          b8Sub = (b8Sub + 1) % 3;
          if (b8Sub == 2 && !ps.globals[int(Global::SysInfo)]) b8Sub = 0;
          return;
        }
        fnPage = id;  // 9–11, 15, 16 show their question
        return;
      default: return;
    }
  }

  // ---- mode buttons ----
  switch (id) {
    case kBtnMemInt:
      if (mode == kModeEdit || mode == kModeFunction) leaveEdit(), mode = kModePlay;
      ps.selMem = kBankInt;
      return;
    case kBtnMemCrt:
      if (!ps.crtPresent) {
        showMessage(Msg::NotReady);
        return;
      }
      if (mode == kModeEdit || mode == kModeFunction) leaveEdit(), mode = kModePlay;
      ps.selMem = kBankCrt;
      return;
    case kBtnEdit:
      if (mode == kModePlay || mode == kModeFunction) {
        mode = kModeEdit;
        armed = false;
      } else if (mode == kModeEdit) {
        if (comparing) comparing = false;  // the edit buffer sounds again, last edit page returns
        else if (ps.edited) {
          comparing = true;
          armed = false;
        }
      }
      return;
    case kBtnFunction:
      if (mode == kModePlay || mode == kModeEdit) {
        leaveEdit();
        mode = kModeFunction;
      }
      return;
    case kBtnStore:
      if (mode == kModePlay) dialog = Dialog::Store;
      else if (mode == kModeEdit && !comparing) dialog = Dialog::EgCopy;
      return;
    case kBtnProtInt: dialog = Dialog::ProtInt; return;
    case kBtnProtCrt: dialog = Dialog::ProtCrt; return;
    case kBtnOpSelect:
      if (mode == kModeEdit) {
        for (int k = 1; k <= 6; ++k) {
          const int op = (selOp - 1 + k) % 6 + 1;
          if (opMask & (1 << (6 - op))) {
            selOp = op;
            break;
          }
        }
      }
      return;
    default: return;
  }
}

void Core::Impl::buttonUp(int id) {
  if (id == held) held = 0;
}

void Core::Impl::repeatStep() {
  if (splash || !held) {
    held = 0;
    return;
  }
  src = Source::Panel;
  tx = true;
  msg = Msg::None;
  if (dialog != Dialog::None || !yesNo(held == kBtnYes)) held = 0;
}

bool Core::Impl::yesNo(bool yes) {
  switch (mode) {
    case kModeEditName:
      nameCursor = clampi(nameCursor + (yes ? 1 : -1), 0, 9);
      return true;
    case kModeEdit: {
      if (comparing) return false;
      PageValue pv;
      if (!pageValue(pv)) return false;
      const int cur = getPageValue(pv);
      const int v = pv.style == PageValue::OnOff ? (yes ? 1 : 0) : clampi(cur + (yes ? 1 : -1), pv.min, pv.max);
      setPageValue(pv, v);
      return pv.style != PageValue::OnOff;
    }
    case kModeFunction: {
      if (fnPage == 8 && b8Sub == 1) {
        setGlobal(int(Global::SysInfo), yes ? 1 : 0);
        return false;
      }
      if (fnPage == 8 && b8Sub == 2) {
        if (yes) {
          sendBank();
        }
        return false;
      }
      if (isConfirmPage(fnPage)) {
        if (yes) dialog = Dialog::Confirm;  // " ARE YOU SURE ? "
        return false;
      }
      PageValue pv;
      if (!pageValue(pv)) return false;
      const int cur = getPageValue(pv);
      const int v = pv.style == PageValue::OnOff ? (yes ? 1 : 0) : clampi(cur + (yes ? 1 : -1), pv.min, pv.max);
      setPageValue(pv, v);
      return pv.style != PageValue::OnOff;
    }
    default: return false;
  }
}

void Core::Impl::dataEntry(float pos) {
  if (splash) return;
  if (dialog != Dialog::None) return;  // F-2: ignored while a dialog is pending
  msg = Msg::None;
  if (mode == kModeEditName || mode == kModePlay) return;
  if (mode == kModeEdit && comparing) return;
  PageValue pv;
  if (!pageValue(pv)) return;
  if (!(pos >= 0.f)) pos = 0.f;  // also catches NaN
  if (pos > 1.f) pos = 1.f;
  int v;
  if (pv.style == PageValue::OnOff) v = pos >= 0.5f ? 1 : 0;
  else v = clampi(int(std::floor(double(pos) * double(pv.max) + 0.5)), pv.min, pv.max);
  setPageValue(pv, v);
}

// ============================================================================
// Pages
// ============================================================================

bool Core::Impl::pageValue(PageValue& pv) const {
  pv = PageValue{};
  auto voice = [&pv](int index, int max, PageValue::Style st) {
    pv.kind = PageValue::Voice;
    pv.index = index;
    pv.min = 0;
    pv.max = max;
    pv.style = st;
    return true;
  };
  auto fn = [&pv](int n, PageValue::Style st) {
    pv.kind = PageValue::Function;
    pv.index = n;
    pv.min = 0;
    pv.max = functionMax(n);
    pv.style = st;
    return true;
  };
  auto bit = [&pv](int n, int b) {
    pv.kind = PageValue::FnBit;
    pv.index = n;
    pv.bit = b;
    pv.min = 0;
    pv.max = 1;
    pv.style = PageValue::OnOff;
    return true;
  };
  if (mode == kModeEdit) {
    const int op = selOp;
    switch (editPage) {
      case 7: return voice(kAlgorithm, 31, PageValue::Num);
      case 8: return voice(kFeedback, 7, PageValue::Num);
      case 9: return voice(kLfoWave, 5, PageValue::Choice);
      case 10: return voice(kLfoSpeed, 99, PageValue::Num);
      case 11: return voice(kLfoDelay, 99, PageValue::Num);
      case 12: return voice(kLfoPmd, 99, PageValue::Num);
      case 13: return voice(kLfoAmd, 99, PageValue::Num);
      case 14: return voice(kLfoKeySync, 1, PageValue::OnOff);
      case 15: return voice(kPitchModSens, 7, PageValue::Num);
      case 16: return voice(opParam(op, kOpAms), 3, PageValue::Num);
      case 17:
        if (b17Sub == 0) return voice(opParam(op, kOpMode), 1, PageValue::Choice);
        return voice(kOscKeySync, 1, PageValue::OnOff);
      case 18: return voice(opParam(op, kOpCoarse), 31, PageValue::Num);
      case 19: return voice(opParam(op, kOpFine), 99, PageValue::Num);
      case 20: return voice(opParam(op, kOpDetune), 14, PageValue::Num);
      case 21: return voice(opParam(op, kOpR1 + egStage), 99, PageValue::Num);
      case 22: return voice(opParam(op, kOpL1 + egStage), 99, PageValue::Num);
      case 23: return voice(opParam(op, kOpBreakPoint), 99, PageValue::Num);
      case 24: return voice(opParam(op, scaleSide ? kOpRightCurve : kOpLeftCurve), 3, PageValue::Choice);
      case 25: return voice(opParam(op, scaleSide ? kOpRightDepth : kOpLeftDepth), 99, PageValue::Num);
      case 26: return voice(opParam(op, kOpRateScaling), 7, PageValue::Num);
      case 27: return voice(opParam(op, kOpLevel), 99, PageValue::Num);
      case 28: return voice(opParam(op, kOpKvs), 7, PageValue::Num);
      case 29: return voice(kPegR1 + pegStage, 99, PageValue::Num);
      case 30: return voice(kPegL1 + pegStage, 99, PageValue::Num);
      case 31: return voice(kTranspose, 48, PageValue::Num);
      default: return false;  // 32 outside naming
    }
  }
  if (mode == kModeFunction) {
    switch (fnPage) {
      case 1:
        pv.kind = PageValue::GlobalV;
        pv.index = int(Global::MasterTune);
        pv.min = 0;
        pv.max = 127;
        return true;
      case 2: return fn(kFnMono, PageValue::Choice);
      case 3: return fn(kFnBendRange, PageValue::Num);
      case 4: return fn(kFnBendStep, PageValue::Num);
      case 5: return fn(kFnPortaMode, PageValue::Choice);
      case 6: return fn(kFnGlissando, PageValue::OnOff);
      case 7: return fn(kFnPortaTime, PageValue::Num);
      case 8:
        if (b8Sub != 0) return false;
        pv.kind = PageValue::GlobalV;
        pv.index = int(Global::MidiChannel);
        pv.min = 1;
        pv.max = 16;
        return true;
      case 17: return fn(kFnWheelRange, PageValue::Num);
      case 21: return fn(kFnFootRange, PageValue::Num);
      case 25: return fn(kFnBreathRange, PageValue::Num);
      case 29: return fn(kFnAtRange, PageValue::Num);
      default:
        if (fnPage >= 17 && fnPage <= 32) {
          const int group = (fnPage - 17) / 4;  // 0 wheel, 1 foot, 2 breath, 3 after
          return bit(kFnWheelAssign + 2 * group, (fnPage - 17) % 4 - 1);
        }
        return false;
    }
  }
  return false;
}

int Core::Impl::getPageValue(const PageValue& pv) const {
  switch (pv.kind) {
    case PageValue::Voice:
      if (pv.index == kOpMask) return opMask;
      return effective(pv.index, ps.edit.raw[pv.index]);
    case PageValue::Function: return ps.fn[pv.index - kFnFirst];
    case PageValue::FnBit: return (ps.fn[pv.index - kFnFirst] >> pv.bit) & 1;
    case PageValue::GlobalV: return ps.globals[pv.index];
    default: return 0;
  }
}

void Core::Impl::setPageValue(const PageValue& pv, int v) {
  switch (pv.kind) {
    case PageValue::Voice: writeVoice(pv.index, v); break;
    case PageValue::Function: writeFn(pv.index, v); break;
    case PageValue::FnBit: {
      const int cur = ps.fn[pv.index - kFnFirst];
      writeFn(pv.index, v ? (cur | (1 << pv.bit)) : (cur & ~(1 << pv.bit)));
      break;
    }
    case PageValue::GlobalV: setGlobal(pv.index, v); break;
    default: break;
  }
}

// A press of an EDIT page button 7..32 (F-2): show it, or cycle its sub-page.
void Core::Impl::showEditPage(int page) {
  if (page == editPage) {
    switch (page) {
      case 17: b17Sub ^= 1; break;
      case 21:
      case 22: egStage = (egStage + 1) & 3; break;
      case 29:
      case 30: pegStage = (pegStage + 1) & 3; break;
      case 24:
      case 25: scaleSide ^= 1; break;
      default: break;
    }
  } else {
    editPage = page;
  }
  armed = page == 31;  // F-5: pressing 31 arms (and re-arms); leaving disarms
}

// F-3: show the page of voice parameter `index` (with its sub-page and operator).
void Core::Impl::showParamPage(int index) {
  int page = editPage;
  if (index < 126) {
    const int off = offsetOfIndex(index);
    selOp = opOfIndex(index);
    switch (off) {
      case kOpR1: case kOpR2: case kOpR3: case kOpR4: page = 21; egStage = off - kOpR1; break;
      case kOpL1: case kOpL2: case kOpL3: case kOpL4: page = 22; egStage = off - kOpL1; break;
      case kOpBreakPoint: page = 23; break;
      case kOpLeftDepth: page = 25; scaleSide = 0; break;
      case kOpRightDepth: page = 25; scaleSide = 1; break;
      case kOpLeftCurve: page = 24; scaleSide = 0; break;
      case kOpRightCurve: page = 24; scaleSide = 1; break;
      case kOpRateScaling: page = 26; break;
      case kOpAms: page = 16; break;
      case kOpKvs: page = 28; break;
      case kOpLevel: page = 27; break;
      case kOpMode: page = 17; b17Sub = 0; break;
      case kOpCoarse: page = 18; break;
      case kOpFine: page = 19; break;
      case kOpDetune: page = 20; break;
      default: break;
    }
  } else if (index <= kPegR4) {
    page = 29;
    pegStage = index - kPegR1;
  } else if (index <= kPegL4) {
    page = 30;
    pegStage = index - kPegL1;
  } else {
    switch (index) {
      case kAlgorithm: page = 7; break;
      case kFeedback: page = 8; break;
      case kOscKeySync: page = 17; b17Sub = 1; break;
      case kLfoSpeed: page = 10; break;
      case kLfoDelay: page = 11; break;
      case kLfoPmd: page = 12; break;
      case kLfoAmd: page = 13; break;
      case kLfoKeySync: page = 14; break;
      case kLfoWave: page = 9; break;
      case kPitchModSens: page = 15; break;
      case kTranspose: page = 31; break;
      default:
        if (index >= kName && index < kName + 10) page = 32;
        break;  // operator mask: the last edit page
    }
  }
  if (page != editPage) armed = false;
  editPage = page;
}

int Core::Impl::fnPageFor(int n, int oldV, int newV) const {
  switch (n) {
    case kFnMono: return 2;
    case kFnBendRange: return 3;
    case kFnBendStep: return 4;
    case kFnPortaMode: return 5;
    case kFnGlissando: return 6;
    case kFnPortaTime: return 7;
    case kFnWheelRange: return 17;
    case kFnFootRange: return 21;
    case kFnBreathRange: return 25;
    case kFnAtRange: return 29;
    default: {
      const int group = (n - kFnWheelAssign) / 2;  // assign bytes
      const int diff = (oldV ^ newV) & 7;
      int b = 0;
      while (b < 2 && !(diff & (1 << b))) ++b;
      return 18 + 4 * group + b;
    }
  }
}

// ============================================================================
// Edits
// ============================================================================

void Core::Impl::writeVoice(int index, int value) {
  if (index < 0 || index > kOpMask) return;
  if (index == kOpMask) {
    opMask = uint8_t(value & 0x3F);  // R-P4: never sets EDITED
    return;
  }
  const int v = clampi(value, 0, voiceMax(index));
  if (sysex::setByte(ps.edit, index, uint8_t(v))) ps.edited = 1;
}

void Core::Impl::writeFn(int n, int value) {
  if (n < kFnFirst || n > kFnLast) return;
  ps.fn[n - kFnFirst] = uint8_t(clampi(value, 0, functionMax(n)));
}

void Core::Impl::setGlobal(int g, int value) {
  switch (g) {
    case int(Global::MasterTune): ps.globals[g] = uint8_t(clampi(value, 0, 127)); break;
    case int(Global::MidiChannel): ps.globals[g] = uint8_t(clampi(value, 1, 16)); break;
    case int(Global::Omni):
    case int(Global::ProtectInt):
    case int(Global::ProtectCrt): ps.globals[g] = value ? 1 : 0; break;
    case int(Global::SysInfo):
      ps.globals[g] = value ? 1 : 0;
      if (!value) {
        if (b8Sub == 2) b8Sub = 1;
        for (int i = 0; i < kTxIds; ++i) txPending[i] = false;
      }
      break;
    case int(Global::Volume): ps.globals[g] = uint8_t(clampi(value, 0, 127)); break;
    default: break;
  }
}

void Core::Impl::setSetting(int s, int value) {
  if (s < 0 || s >= int(SettingId::Count)) return;
  int v = value;
  if (s == int(SettingId::Zoom)) {
    if (v != 0) v = (clampi(v, 50, 200) + 5) / 10 * 10;
    if (v > 200) v = 200;
  } else {
    v = v ? 1 : 0;
  }
  ps.settings[s] = uint8_t(v);
  applySettingsStruct();
}

void Core::Impl::cancelDialog() { dialog = Dialog::None; }

void Core::Impl::endNaming() {
  if (mode == kModeEditName) {
    mode = kModeEdit;
    editPage = 32;
  }
}

void Core::Impl::endCompare() { comparing = false; }

void Core::Impl::leaveEdit() {
  comparing = false;
  armed = false;
}

// F-3 / R-S3: cancel any dialog or naming, end COMPARE.
void Core::Impl::touchProgrammer() {
  if (splash) return;
  cancelDialog();
  endNaming();
  endCompare();
}

void Core::Impl::programmerVoice(int index, int value) {
  if (index < 0 || index > kOpMask) return;
  const int v = index == kOpMask ? (value & 0x3F) : clampi(value, 0, voiceMax(index));
  const int cur = index == kOpMask ? opMask : ps.edit.raw[index];
  if (v == cur) return;  // touching = changing a value
  tx = true;
  touchProgrammer();
  writeVoice(index, v);
  if (splash) return;
  if (mode == kModePlay || mode == kModeFunction) {
    mode = kModeEdit;
    showParamPage(index);
  } else if (index < 126) {
    selOp = opOfIndex(index);
  }
}

void Core::Impl::programmerFn(int n, int value) {
  if (n < kFnFirst || n > kFnLast) return;
  const int v = clampi(value, 0, functionMax(n));
  const int cur = ps.fn[n - kFnFirst];
  if (v == cur) return;
  tx = true;
  touchProgrammer();
  writeFn(n, v);
  if (splash) return;
  if (mode != kModeFunction) {
    leaveEdit();
    mode = kModeFunction;
    fnPage = fnPageFor(n, cur, v);
  }
}

// R-S2: host automation, SysEx parameter change, CC5.
void Core::Impl::externalVoice(int index, int value) {
  if (index < 0 || index > kOpMask) return;
  const int v = index == kOpMask ? (value & 0x3F) : clampi(value, 0, voiceMax(index));
  const int cur = index == kOpMask ? opMask : ps.edit.raw[index];
  if (v == cur) return;
  endCompare();
  writeVoice(index, v);
}

void Core::Impl::externalFn(int n, int value) {
  if (n < kFnFirst || n > kFnLast) return;
  const int v = clampi(value, 0, functionMax(n));
  if (v == ps.fn[n - kFnFirst]) return;
  endCompare();
  writeFn(n, v);
}

void Core::Impl::panelVoice(int index, int value) {
  tx = true;
  writeVoice(index, value);
}

// ============================================================================
// Voices and banks
// ============================================================================

uint8_t* Core::Impl::bankData(int bank) { return bank == kBankCrt ? ps.crtBank : ps.intBank; }

bool Core::Impl::bankProtected(int bank) const {
  return ps.globals[bank == kBankCrt ? int(Global::ProtectCrt) : int(Global::ProtectInt)] != 0;
}

// F-6 / R-P5.
void Core::Impl::selectVoice(int bank, int slot, bool setSelMem) {
  if (bank != kBankInt && bank != kBankCrt) return;
  if (slot < 1 || slot > 32) return;
  if (bank == kBankCrt && !ps.crtPresent) return;
  tx = false;
  if (ps.edited) ps.recall = ps.edit;  // R-P6
  sysex::unpackVoice(bankData(bank) + (slot - 1) * sysex::kPackedVoiceBytes, ps.edit);
  ps.stored = ps.edit;  // R-P11
  ps.srcBank = uint8_t(bank);
  ps.srcSlot = uint8_t(slot);
  ps.edited = 0;
  opMask = 0x3F;  // R-P7
  comparing = false;
  endNaming();
  if (setSelMem) ps.selMem = uint8_t(bank);
}

// A voice loaded into the edit buffer that is not a selection (received or
// file-loaded voice, EDIT RECALL, VOICE INIT): EDITED, mask all-on.
void Core::Impl::loadEditBuffer(const sysex::Voice& v, bool captureRecall) {
  tx = false;
  if (captureRecall && ps.edited) ps.recall = ps.edit;
  ps.edit = v;
  ps.edited = 1;
  opMask = 0x3F;
  comparing = false;
}

// F-14.
void Core::Impl::programChange(int p) {
  if (p < 0 || p > 63) return;
  if (p >= 32 && !ps.crtPresent) return;
  cancelDialog();
  selectVoice(p < 32 ? kBankInt : kBankCrt, p % 32 + 1, true);
}

// F-7.
void Core::Impl::doStore(int slot) {
  tx = false;
  const int bank = ps.selMem;
  if (bank == kBankCrt && !ps.crtPresent) {
    showMessage(Msg::NotReady);
    return;
  }
  if (bankProtected(bank)) {
    showMessage(Msg::Protected);
    return;
  }
  sysex::packVoice(ps.edit, bankData(bank) + (slot - 1) * sysex::kPackedVoiceBytes);
  ps.stored = ps.edit;
  ps.srcBank = uint8_t(bank);
  ps.srcSlot = uint8_t(slot);
  ps.edited = 0;
  if (bank == kBankCrt) ps.crtChanged = 1;
}

// F-8 / R-U2 EG copy: R1–R4 and L1–L4.
// A copy to a different operator always makes the buffer EDITED (F-8.2), even
// when the EG bytes were already equal.
void Core::Impl::doEgCopy(int fromOp, int toOp) {
  if (fromOp < 1 || fromOp > 6 || toOp < 1 || toOp > 6 || fromOp == toOp) return;
  for (int k = 0; k < 8; ++k) writeVoice(opParam(toOp, k), ps.edit.raw[opParam(fromOp, k)]);
  ps.edited = 1;
}

// F-10.4.
void Core::Impl::executeConfirm() {
  tx = false;
  switch (fnPage) {
    case 9: {
      const sysex::Voice r = ps.recall;
      loadEditBuffer(r, false);
      showMessage(Msg::Completed);
      break;
    }
    case 10: {
      sysex::Voice v;
      sysex::initVoice(v);
      loadEditBuffer(v, false);
      showMessage(Msg::Completed);
      break;
    }
    case 11:
    case 15:
      if (!ps.crtPresent) showMessage(Msg::NotReady);
      else if (bankProtected(kBankCrt)) showMessage(Msg::Protected);
      else {
        if (fnPage == 11) {
          sysex::Voice v;
          sysex::initVoice(v);
          for (int s = 0; s < 32; ++s) sysex::packVoice(v, ps.crtBank + s * sysex::kPackedVoiceBytes);
        } else {
          std::memcpy(ps.crtBank, ps.intBank, sysex::kPackedBankBytes);
        }
        ps.crtChanged = 1;
        showMessage(Msg::Completed);
      }
      break;
    case 16:
      if (!ps.crtPresent) showMessage(Msg::NotReady);
      else if (bankProtected(kBankInt)) showMessage(Msg::Protected);
      else {
        std::memcpy(ps.intBank, ps.crtBank, sysex::kPackedBankBytes);
        showMessage(Msg::Completed);
      }
      break;
    default: break;
  }
}

// R-U2 paste / init operator: the operator's 21 bytes (clamped to range).
void Core::Impl::writeOperator(int op, const uint8_t* bytes21) {
  if (op < 1 || op > 6) return;
  touchProgrammer();
  tx = src == Source::Programmer || src == Source::Panel;
  for (int k = 0; k < kOpBytes; ++k) writeVoice(opParam(op, k), bytes21[k]);
}

// R-U2 swap: exchange all 21 bytes of two operators.
void Core::Impl::swapOperators(int a, int b) {
  if (a < 1 || a > 6 || b < 1 || b > 6 || a == b) return;
  touchProgrammer();
  tx = src == Source::Programmer || src == Source::Panel;
  for (int k = 0; k < kOpBytes; ++k) {
    const uint8_t x = ps.edit.raw[opParam(a, k)];
    const uint8_t y = ps.edit.raw[opParam(b, k)];
    if (sysex::setByte(ps.edit, opParam(a, k), y)) ps.edited = 1;
    if (sysex::setByte(ps.edit, opParam(b, k), x)) ps.edited = 1;
  }
}

// R-U3 Init voice: as FUNCTION 10 without confirmation.
void Core::Impl::initVoiceCmd() {
  touchProgrammer();
  sysex::Voice v;
  sysex::initVoice(v);
  loadEditBuffer(v, false);
}

uint32_t Core::Impl::nextRandom() {
  // xorshift32
  uint32_t x = rngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rngState = x;
  return x;
}

// R-U3 Randomise.
void Core::Impl::randomise(int seed) {
  if (seed != 0) rngState = uint32_t(seed) * 2654435761u ^ 0x9E3779B9u;
  if (rngState == 0) rngState = 1;
  touchProgrammer();
  tx = src == Source::Programmer || src == Source::Panel;
  auto uniform = [this](int lo, int hi) { return lo + int(nextRandom() % uint32_t(hi - lo + 1)); };
  const int alg = uniform(0, 31);
  writeVoice(kAlgorithm, alg);
  writeVoice(kFeedback, uniform(0, 7));
  const uint8_t carriers = sysex::carrierMask(alg);
  for (int op = 1; op <= 6; ++op) {
    uint8_t o[kOpBytes];
    sysex::initOperatorRaw(o, 0);
    o[kOpMode] = 0;
    o[kOpFine] = 0;
    o[kOpDetune] = uint8_t(uniform(4, 10));
    // coarse from {0, 1..8}: weight 3 for 1 and 2, 1 for the others (13 in total)
    static constexpr uint8_t kCoarse[13] = {0, 1, 1, 1, 2, 2, 2, 3, 4, 5, 6, 7, 8};
    o[kOpCoarse] = kCoarse[uniform(0, 12)];
    o[kOpLevel] = uint8_t((carriers >> (op - 1)) & 1 ? uniform(60, 99) : uniform(0, 90));
    for (int k = 0; k < 4; ++k) o[kOpR1 + k] = uint8_t(uniform(30, 99));
    o[kOpL1] = 99;
    o[kOpL2] = uint8_t(uniform(50, 99));
    o[kOpL3] = uint8_t(uniform(0, 99));
    o[kOpL4] = 0;
    o[kOpKvs] = uint8_t(uniform(0, 3));
    for (int k = 0; k < kOpBytes; ++k) writeVoice(opParam(op, k), o[k]);
  }
  writeVoice(kOpMask, 0x3F);
  ps.edited = 1;
}

// R-U3 Compare: as EDIT/COMPARE (enters EDIT if needed); disabled while CLEAN.
void Core::Impl::toggleCompare() {
  if (splash) return;
  if (comparing) {
    comparing = false;
    return;
  }
  if (!ps.edited) return;
  cancelDialog();
  endNaming();
  if (mode != kModeEdit) {
    leaveEdit();
    mode = kModeEdit;
  }
  comparing = true;
  armed = false;
}

// Bank manager row drop (R-P9).
void Core::Impl::copyVoice(int fromBank, int fromSlot, int toBank, int toSlot) {
  if (fromSlot < 1 || fromSlot > 32 || toSlot < 1 || toSlot > 32) return;
  if ((fromBank != kBankInt && fromBank != kBankCrt) || (toBank != kBankInt && toBank != kBankCrt)) return;
  if ((fromBank == kBankCrt || toBank == kBankCrt) && !ps.crtPresent) {
    showMessage(Msg::NotReady);
    return;
  }
  cancelDialog();
  if (bankProtected(toBank)) {
    showMessage(Msg::Protected);
    notice(kNoticeProtected, toBank);
    return;
  }
  uint8_t tmp[sysex::kPackedVoiceBytes];
  std::memcpy(tmp, bankData(fromBank) + (fromSlot - 1) * sysex::kPackedVoiceBytes, sizeof tmp);
  std::memcpy(bankData(toBank) + (toSlot - 1) * sysex::kPackedVoiceBytes, tmp, sizeof tmp);
  if (toBank == kBankCrt) ps.crtChanged = 1;
}

// F-12.1 voice file: to the edit buffer as a voice load (recall capture), EDITED.
void Core::Impl::loadVoiceFile(const uint8_t* data, int len, bool badSum) {
  if (!data || len < sysex::kVoiceBytes) return;
  cancelDialog();
  endNaming();
  sysex::Voice v;
  sysex::voiceFromUnpacked(data, v);
  loadEditBuffer(v, true);
  if (badSum) showMessage(Msg::ChecksumError);
}

// F-12.1 bank file: insert as cartridge (no protect check) or load into internal.
void Core::Impl::loadBankFile(int bank, bool badSum, const uint8_t* data, int len) {
  if (!data || len < sysex::kPackedBankBytes) return;
  cancelDialog();
  if (bank == kBankCrt) {
    ps.crtPresent = 1;
    ps.crtChanged = 0;
    std::memcpy(ps.crtBank, data, sysex::kPackedBankBytes);
    std::memset(ps.crtName, 0, sizeof ps.crtName);
    int n = 0;
    while (n < state::kCrtNameMax && sysex::kPackedBankBytes + n < len && data[sysex::kPackedBankBytes + n] != 0) {
      ps.crtName[n] = char(data[sysex::kPackedBankBytes + n]);
      ++n;
    }
    ps.crtNameLen = uint8_t(n);
  } else {
    if (bankProtected(kBankInt)) {
      showMessage(Msg::Protected);
      notice(kNoticeProtected, kBankInt);
      return;
    }
    std::memcpy(ps.intBank, data, sysex::kPackedBankBytes);
  }
  if (badSum) showMessage(Msg::ChecksumError);
}

void Core::Impl::newCartridge() {
  sysex::Voice v;
  sysex::initVoice(v);
  for (int s = 0; s < 32; ++s) sysex::packVoice(v, ps.crtBank + s * sysex::kPackedVoiceBytes);
  ps.crtPresent = 1;
  ps.crtChanged = 0;
  std::memset(ps.crtName, 0, sizeof ps.crtName);
  std::memcpy(ps.crtName, "UNTITLED", 8);
  ps.crtNameLen = 8;
}

void Core::Impl::ejectCartridge() {
  if (!ps.crtPresent) return;
  ps.crtPresent = 0;
  ps.crtChanged = 0;
  ps.crtNameLen = 0;
  std::memset(ps.crtName, 0, sizeof ps.crtName);
  std::memset(ps.crtBank, 0, sizeof ps.crtBank);
  if (ps.selMem == kBankCrt) ps.selMem = kBankInt;  // the voice source keeps showing CRT
}

namespace {
// F-12.5: characters outside A–Z a–z 0–9 - _ space become '_'.
int sanitise(const char* in, int n, char* out, int cap) {
  int k = 0;
  for (int i = 0; i < n && k < cap; ++i) {
    const char c = in[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                    c == ' ';
    out[k++] = ok ? c : '_';
  }
  return k;
}
}  // namespace

void Core::Impl::exportData(int kind) {
  uint8_t* buf = msgBuf;
  Event e;
  e.type = EvType::ExportData;
  e.src = src;
  e.a = kind;
  int len = 0;
  char name[64];
  int n = 0;
  if (kind == int(ExportKind::Internal)) {
    len = sysex::buildBankDump(ps.intBank, 0, buf);
    n = sanitise("INTERNAL", 8, name, 59);
  } else if (kind == int(ExportKind::Cartridge)) {
    if (!ps.crtPresent) return;
    len = sysex::buildBankDump(ps.crtBank, 0, buf);
    n = sanitise(ps.crtName, ps.crtNameLen, name, 59);
    if (n == 0) n = sanitise("CARTRIDGE", 9, name, 59);
  } else if (kind == int(ExportKind::Voice)) {
    len = sysex::buildVoiceDump(ps.edit.raw, 0, buf);
    char disp[10];
    sysex::displayName(ps.edit.raw, disp);
    int b = 0, t = 10;
    while (b < t && disp[b] == ' ') ++b;
    while (t > b && disp[t - 1] == ' ') --t;
    n = sanitise(disp + b, t - b, name, 59);
    if (n == 0) n = sanitise("VOICE", 5, name, 59);
  } else {
    return;
  }
  std::memcpy(e.text, name, size_t(n));
  std::memcpy(e.text + n, ".syx", 5);
  ev->pushPayload(e, buf, len);
}

void Core::Impl::sendBank() {
  sendMidi(msgBuf, sysex::buildBankDump(ps.intBank, 0, msgBuf));
  showMessage(Msg::Completed);
  notice(kNoticeCompleted, 0);
}

void Core::Impl::startSplash() {
  resetPanel();
  splash = true;
  splashUntil = now + tSplash;
}

// F-1.5.
void Core::Impl::endSplash() {
  splash = false;
  splashDone = true;
  resetPanel();
  ps.selMem = (ps.srcBank == kBankCrt && ps.crtPresent) ? kBankCrt : kBankInt;
  if (stateResetPending) {
    stateResetPending = false;
    showMessage(Msg::StateReset);
  }
}

void Core::Impl::showMessage(Msg m) {
  if (splash) return;  // the splash has the LCD; messages during it are dropped
  msg = m;
  msgUntil = now + tMsg;
}

void Core::Impl::notice(int code, int b) {
  Event e;
  e.type = EvType::Notice;
  e.src = src;
  e.a = code;
  e.b = b;
  push(e);
}

}  // namespace vdx

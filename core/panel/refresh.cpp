// Engine sync, diff-based event emission (vdx.h EvType), timers, meters and
// the R-S5 snapshot. After every command, MIDI message and block the core
// compares its state with what it last emitted and reports the differences.
#include <cmath>
#include <cstring>

#include "panel/core_impl.h"
#include "panel/format.h"

namespace vdx {

using namespace params;

namespace {

void copyText(Event& e, const char* s, int maxLen = 63) {
  int n = 0;
  while (s[n] && n < maxLen) {
    e.text[n] = s[n];
    ++n;
  }
  if (n < 64) e.text[n] = 0;
}

fm::FunctionState functionState(const uint8_t* fn) {
  fm::FunctionState f;
  f.mono = fn[0];
  f.bendRange = fn[1];
  f.bendStep = fn[2];
  f.portaMode = fn[3];
  f.gliss = fn[4];
  f.portaTime = fn[5];
  f.wheelRange = fn[6];
  f.wheelAssign = fn[7];
  f.footRange = fn[8];
  f.footAssign = fn[9];
  f.breathRange = fn[10];
  f.breathAssign = fn[11];
  f.atRange = fn[12];
  f.atAssign = fn[13];
  return f;
}

bool sameFn(const fm::FunctionState& a, const fm::FunctionState& b) {
  return a.mono == b.mono && a.bendRange == b.bendRange && a.bendStep == b.bendStep && a.portaMode == b.portaMode &&
         a.gliss == b.gliss && a.portaTime == b.portaTime && a.wheelRange == b.wheelRange &&
         a.wheelAssign == b.wheelAssign && a.footRange == b.footRange && a.footAssign == b.footAssign &&
         a.breathRange == b.breathRange && a.breathAssign == b.breathAssign && a.atRange == b.atRange &&
         a.atAssign == b.atAssign;
}

bool sameLcd(const LcdState& a, const LcdState& b) {
  return std::memcmp(a.l1, b.l1, 16) == 0 && std::memcmp(a.l2, b.l2, 16) == 0 && a.cursor == b.cursor &&
         (!a.cursor || (a.row == b.row && a.col == b.col));
}

}  // namespace

void Core::Impl::editEffective(uint8_t* eff) const {
  sysex::effectiveVoice(ps.edit.raw, eff);
  eff[kOpMask] = opMask;
}

void Core::Impl::push(const Event& e) { ev->push(e); }

void Core::Impl::syncEngine() {
  uint8_t v[156];
  if (comparing) {  // F-9: the stored voice sounds, all operators on
    sysex::effectiveVoice(ps.stored.raw, v);
    v[kOpMask] = 0x3F;
  } else {
    editEffective(v);
  }
  if (!engineValid || std::memcmp(v, engVoice, sizeof v) != 0) {
    std::memcpy(engVoice, v, sizeof v);
    engine.setVoice(engVoice);
  }
  const fm::FunctionState f = functionState(ps.fn);
  if (!engineValid || !sameFn(f, engFn)) {
    engFn = f;
    engine.setFunction(engFn);
  }
  const int tune = ps.globals[int(Global::MasterTune)];
  if (!engineValid || tune != engTune) {
    engTune = tune;
    engine.setMasterTune(tune);
  }
  const int m0 = int(settings.engine), m1 = settings.dac ? 1 : 0, m2 = settings.classicVelocity ? 1 : 0;
  if (!engineValid || m0 != engMode[0] || m1 != engMode[1] || m2 != engMode[2]) {
    engMode[0] = m0;
    engMode[1] = m1;
    engMode[2] = m2;
    engine.setMode(settings.engine, settings.dac, settings.classicVelocity);
  }
  if (!engineValid) engine.portamentoSwitch(portaOn);  // CC65 defaults to on (R-E12)
  engineValid = true;
}

void Core::Impl::refresh() {
  syncEngine();

  uint8_t eff[156];
  editEffective(eff);
  // ---- parameters (one event per changed parameter) ----
  for (int i = 0; i < 156; ++i) {
    if (eff[i] == lastEff[i]) continue;
    lastEff[i] = eff[i];
    Event e;
    e.type = EvType::VoiceParam;
    e.src = src;
    e.a = i;
    e.b = eff[i];
    fmt::voiceString(eff, i, eff[i], e.text);
    push(e);
    if (tx) queueTx(i);
  }
  for (int k = 0; k < 14; ++k) {
    if (ps.fn[k] == lastFn[k]) continue;
    lastFn[k] = ps.fn[k];
    Event e;
    e.type = EvType::FunctionParam;
    e.src = src;
    e.a = kFnFirst + k;
    e.b = ps.fn[k];
    fmt::functionString(ps.fn, kFnFirst + k, ps.fn[k], e.text);
    push(e);
    if (tx) queueTx(156 + k);
  }
  for (int g = 0; g < 7; ++g) {
    if (ps.globals[g] == lastGlobals[g]) continue;
    lastGlobals[g] = ps.globals[g];
    Event e;
    e.type = EvType::GlobalValue;
    e.src = src;
    e.a = g;
    e.b = ps.globals[g];
    fmt::globalString(g, ps.globals[g], e.text);
    push(e);
  }
  for (int s = 0; s < 7; ++s) {
    if (ps.settings[s] == lastSettings[s]) continue;
    lastSettings[s] = ps.settings[s];
    Event e;
    e.type = EvType::SettingValue;
    e.src = src;
    e.a = s;
    e.b = ps.settings[s];
    push(e);
  }
  // ---- derived displays ----
  for (int op = 1; op <= 6; ++op) {
    char f[fmt::kMaxText], t[fmt::kMaxText];
    fmt::opFrequency(eff[opParam(op, kOpMode)], eff[opParam(op, kOpCoarse)], eff[opParam(op, kOpFine)], f);
    fmt::trim(f, t);
    if (std::strcmp(t, lastOpFreq[op - 1]) == 0) continue;
    std::strcpy(lastOpFreq[op - 1], t);
    Event e;
    e.type = EvType::OpFrequency;
    e.src = src;
    e.a = op;
    copyText(e, t);
    push(e);
  }
  if (eff[kLfoSpeed] != lastLfoSpeed) {
    lastLfoSpeed = eff[kLfoSpeed];
    Event e;
    e.type = EvType::LfoHz;
    e.src = src;
    e.a = lastLfoSpeed;
    fmt::lfoHz(lastLfoSpeed, e.text);
    push(e);
  }
  // ---- banks ----
  for (int b = 0; b < 2; ++b) {
    if (b == kBankCrt && !ps.crtPresent) {
      lastNamesValid[b] = false;
      continue;
    }
    const uint8_t* bank = b == kBankCrt ? ps.crtBank : ps.intBank;
    for (int s = 0; s < 32; ++s) {
      char name[10];
      sysex::packedName(bank + s * sysex::kPackedVoiceBytes, name);
      if (lastNamesValid[b] && std::memcmp(name, lastNames[b][s], 10) == 0) continue;
      std::memcpy(lastNames[b][s], name, 10);
      Event e;
      e.type = EvType::VoiceName;
      e.src = src;
      e.a = b;
      e.b = s + 1;
      std::memcpy(e.text, name, 10);
      e.text[10] = 0;
      push(e);
    }
    lastNamesValid[b] = true;
  }
  if (ps.crtPresent != lastCrtPresent || ps.crtChanged != lastCrtChanged ||
      std::strncmp(lastCrtName, ps.crtName, 64) != 0 || int(std::strlen(lastCrtName)) != ps.crtNameLen) {
    lastCrtPresent = ps.crtPresent;
    lastCrtChanged = ps.crtChanged;
    std::memset(lastCrtName, 0, sizeof lastCrtName);
    std::memcpy(lastCrtName, ps.crtName, ps.crtNameLen);
    Event e;
    e.type = EvType::Cartridge;
    e.src = src;
    e.a = ps.crtPresent;
    e.b = ps.crtChanged;
    std::memcpy(e.text, ps.crtName, ps.crtNameLen);
    if (ps.crtNameLen < 64) e.text[ps.crtNameLen] = 0;
    push(e);
  }
  {
    const int m[4] = {ps.selMem, ps.srcBank, ps.srcSlot, ps.edited};
    if (std::memcmp(m, lastMem, sizeof m) != 0) {
      std::memcpy(lastMem, m, sizeof m);
      Event e;
      e.type = EvType::Memory;
      e.src = src;
      e.a = m[0];
      e.b = m[1];
      e.c = m[2];
      e.d = m[3];
      push(e);
    }
  }
  // ---- LCD, LED, lamps, panel ----
  {
    LcdState l;
    renderLcd(l);
    if (!lastLcdValid || !sameLcd(l, lastLcd)) {
      lastLcd = l;
      lastLcdValid = true;
      Event e;
      e.type = EvType::Lcd;
      e.src = src;
      std::memcpy(e.text, l.l1, 16);
      std::memcpy(e.text + 16, l.l2, 16);
      e.text[32] = 0;
      e.a = l.cursor ? l.row : 0;
      e.b = l.cursor ? l.col : 0;
      e.c = l.cursor ? 1 : 0;
      e.d = 1;
      push(e);
    }
  }
  {
    const int led[2] = {splash ? 0 : ps.srcSlot, (!splash && ps.edited && !comparing) ? 1 : 0};
    if (led[0] != lastLed[0] || led[1] != lastLed[1]) {
      lastLed[0] = led[0];
      lastLed[1] = led[1];
      Event e;
      e.type = EvType::Led;
      e.src = src;
      e.a = led[0];
      e.b = led[1];
      push(e);
    }
  }
  {
    int l[3];
    lamps(l[0], l[1], l[2]);
    if (std::memcmp(l, lastLamps, sizeof l) != 0) {
      std::memcpy(lastLamps, l, sizeof l);
      Event e;
      e.type = EvType::Lamps;
      e.src = src;
      e.a = l[0];
      e.b = l[1];
      e.c = l[2];
      push(e);
    }
  }
  {
    int flags = 0;
    if (comparing) flags |= kFlagComparing;
    if (ps.edited) flags |= kFlagEdited;
    if (dialog != Dialog::None) flags |= kFlagDialog;
    if (msg != Msg::None) flags |= kFlagMessage;
    if (armed) flags |= kFlagTransposeArmed;
    const int page = splash ? 0 : (mode == kModeEdit || mode == kModeEditName) ? editPage : (mode == kModeFunction ? fnPage : 0);
    const int p[4] = {splash ? int(kModeSplash) : mode, page, selOp, flags};
    if (std::memcmp(p, lastPanel, sizeof p) != 0) {
      std::memcpy(lastPanel, p, sizeof p);
      Event e;
      e.type = EvType::PanelState;
      e.src = src;
      e.a = p[0];
      e.b = p[1];
      e.c = p[2];
      e.d = p[3];
      push(e);
    }
  }
  {
    PageValue pv;
    if (!splash && !(mode == kModeEdit && comparing) && pageValue(pv)) {
      const int key = (int(pv.kind) << 16) | (pv.index << 4) | pv.bit;
      const int v = getPageValue(pv);
      if (key != lastDeKey || v != lastDeVal) {
        lastDeKey = key;
        lastDeVal = v;
        Event e;
        e.type = EvType::DataEntryPos;
        e.src = src;
        e.f = pv.max > 0 ? float(v) / float(pv.max) : 0.f;
        if (e.f > 1.f) e.f = 1.f;
        push(e);
      }
    } else {
      lastDeKey = -1;  // a page without a value: the next value page re-sends the thumb
    }
  }
  // ---- saved state ----
  if (std::memcmp(&ps, &shadowPs, sizeof ps) != 0) {
    std::memcpy(&shadowPs, &ps, sizeof ps);
    dirtyForInstrument = true;
    Event e;
    e.type = EvType::StateDirty;
    e.src = src;
    push(e);
  }
}

void Core::Impl::syncShadows() {
  editEffective(lastEff);
  std::memcpy(lastFn, ps.fn, sizeof lastFn);
  std::memcpy(lastGlobals, ps.globals, sizeof lastGlobals);
  std::memcpy(lastSettings, ps.settings, sizeof lastSettings);
  for (int op = 1; op <= 6; ++op) {
    char f[fmt::kMaxText];
    fmt::opFrequency(lastEff[opParam(op, kOpMode)], lastEff[opParam(op, kOpCoarse)], lastEff[opParam(op, kOpFine)], f);
    fmt::trim(f, lastOpFreq[op - 1]);
  }
  lastLfoSpeed = lastEff[kLfoSpeed];
  for (int b = 0; b < 2; ++b) {
    lastNamesValid[b] = b == kBankInt || ps.crtPresent;
    if (!lastNamesValid[b]) continue;
    const uint8_t* bank = b == kBankCrt ? ps.crtBank : ps.intBank;
    for (int s = 0; s < 32; ++s) sysex::packedName(bank + s * sysex::kPackedVoiceBytes, lastNames[b][s]);
  }
  lastCrtPresent = ps.crtPresent;
  lastCrtChanged = ps.crtChanged;
  std::memset(lastCrtName, 0, sizeof lastCrtName);
  std::memcpy(lastCrtName, ps.crtName, ps.crtNameLen);
  lastMem[0] = ps.selMem;
  lastMem[1] = ps.srcBank;
  lastMem[2] = ps.srcSlot;
  lastMem[3] = ps.edited;
  renderLcd(lastLcd);
  lastLcdValid = true;
  lastLed[0] = splash ? 0 : ps.srcSlot;
  lastLed[1] = (!splash && ps.edited && !comparing) ? 1 : 0;
  lamps(lastLamps[0], lastLamps[1], lastLamps[2]);
  int flags = ps.edited ? kFlagEdited : 0;
  lastPanel[0] = mode;
  lastPanel[1] = 0;
  lastPanel[2] = selOp;
  lastPanel[3] = flags;
  lastDeKey = -1;
  lastDeVal = -1;
  std::memcpy(&shadowPs, &ps, sizeof ps);
  engineValid = false;
}

// R-S5: the complete state, then SnapshotEnd.
void Core::Impl::emitSnapshot() {
  const Source saved = src;
  src = Source::State;
  uint8_t eff[156];
  editEffective(eff);
  for (int i = 0; i < 156; ++i) {
    Event e;
    e.type = EvType::VoiceParam;
    e.src = src;
    e.a = i;
    e.b = eff[i];
    fmt::voiceString(eff, i, eff[i], e.text);
    push(e);
  }
  for (int k = 0; k < 14; ++k) {
    Event e;
    e.type = EvType::FunctionParam;
    e.src = src;
    e.a = kFnFirst + k;
    e.b = ps.fn[k];
    fmt::functionString(ps.fn, kFnFirst + k, ps.fn[k], e.text);
    push(e);
  }
  for (int g = 0; g < 7; ++g) {
    Event e;
    e.type = EvType::GlobalValue;
    e.src = src;
    e.a = g;
    e.b = ps.globals[g];
    fmt::globalString(g, ps.globals[g], e.text);
    push(e);
  }
  for (int s = 0; s < 7; ++s) {
    Event e;
    e.type = EvType::SettingValue;
    e.src = src;
    e.a = s;
    e.b = ps.settings[s];
    push(e);
  }
  for (int op = 1; op <= 6; ++op) {
    Event e;
    e.type = EvType::OpFrequency;
    e.src = src;
    e.a = op;
    copyText(e, lastOpFreq[op - 1]);
    push(e);
  }
  {
    Event e;
    e.type = EvType::LfoHz;
    e.src = src;
    e.a = eff[kLfoSpeed];
    fmt::lfoHz(eff[kLfoSpeed], e.text);
    push(e);
  }
  for (int b = 0; b < 2; ++b) {
    if (b == kBankCrt && !ps.crtPresent) continue;
    const uint8_t* bank = b == kBankCrt ? ps.crtBank : ps.intBank;
    for (int s = 0; s < 32; ++s) {
      Event e;
      e.type = EvType::VoiceName;
      e.src = src;
      e.a = b;
      e.b = s + 1;
      sysex::packedName(bank + s * sysex::kPackedVoiceBytes, e.text);
      e.text[10] = 0;
      push(e);
    }
  }
  {
    Event e;
    e.type = EvType::Cartridge;
    e.src = src;
    e.a = ps.crtPresent;
    e.b = ps.crtChanged;
    std::memcpy(e.text, ps.crtName, ps.crtNameLen);
    if (ps.crtNameLen < 64) e.text[ps.crtNameLen] = 0;
    push(e);
  }
  {
    Event e;
    e.type = EvType::Memory;
    e.src = src;
    e.a = ps.selMem;
    e.b = ps.srcBank;
    e.c = ps.srcSlot;
    e.d = ps.edited;
    push(e);
  }
  {
    Event e;
    e.type = EvType::Wheels;
    e.src = src;
    e.a = bend;
    e.b = modWheel;
    push(e);
  }
  {
    Event e;
    e.type = EvType::Lcd;
    e.src = src;
    std::memcpy(e.text, lastLcd.l1, 16);
    std::memcpy(e.text + 16, lastLcd.l2, 16);
    e.text[32] = 0;
    e.a = lastLcd.cursor ? lastLcd.row : 0;
    e.b = lastLcd.cursor ? lastLcd.col : 0;
    e.c = lastLcd.cursor ? 1 : 0;
    e.d = 1;
    push(e);
  }
  {
    Event e;
    e.type = EvType::Led;
    e.src = src;
    e.a = lastLed[0];
    e.b = lastLed[1];
    push(e);
  }
  {
    Event e;
    e.type = EvType::Lamps;
    e.src = src;
    e.a = lastLamps[0];
    e.b = lastLamps[1];
    e.c = lastLamps[2];
    push(e);
  }
  {
    Event e;
    e.type = EvType::PanelState;
    e.src = src;
    e.a = lastPanel[0];
    e.b = lastPanel[1];
    e.c = lastPanel[2];
    e.d = lastPanel[3];
    push(e);
  }
  {
    PageValue pv;
    Event e;
    e.type = EvType::DataEntryPos;
    e.src = src;
    if (pageValue(pv)) {
      const int v = getPageValue(pv);
      e.f = pv.max > 0 ? float(v) / float(pv.max) : 0.f;
      if (e.f > 1.f) e.f = 1.f;
    }
    push(e);
  }
  Event end;
  end.type = EvType::SnapshotEnd;
  end.src = src;
  push(end);
  src = saved;
}

void Core::Impl::processTimers() {
  src = Source::Internal;
  tx = false;
  if (splash && now >= splashUntil) endSplash();
  if (msg != Msg::None && now >= msgUntil) msg = Msg::None;
  if (held && now >= nextRepeat) {
    nextRepeat += tRepeatPeriod;
    repeatStep();
  }
  flushTx();
}

void Core::Impl::meters() {
  if (now < nextMeter) return;
  nextMeter = now + tMeter;
  int32_t peak[6];
  engine.takeOperatorPeaks(peak);
  if (ev->fill() > 0.75f) return;  // meters are dropped first
  int16_t db[6];
  bool silent = true;
  for (int i = 0; i < 6; ++i) {
    int v = -6000;
    if (peak[i] > 0) {
      const double d = 2000.0 * std::log10(double(peak[i]) / 134217728.0);  // 2^27 = 0 dBFS
      v = d < -6000.0 ? -6000 : (d > 0.0 ? 0 : int(std::lround(d)));
    }
    db[i] = int16_t(v);
    if (v > -6000) silent = false;
  }
  if (silent && lastMeterSilent) return;
  lastMeterSilent = silent;
  Event e;
  e.type = EvType::Meter;
  e.src = Source::Internal;
  for (int i = 0; i < 6; ++i) {
    const uint16_t u = uint16_t(db[i]);
    e.text[2 * i] = char(u & 0xFF);
    e.text[2 * i + 1] = char(u >> 8);
  }
  push(e);
}

}  // namespace vdx

// MIDI receive (SPEC §7.2 R-M1), SysEx over MIDI (F-13), transmit (R-M3, F-13.6).
#include <cstring>

#include "panel/core_impl.h"
#include "sysex/sysex.h"

namespace vdx {

using namespace params;

void Core::Impl::midiMessage(const uint8_t* m, int len) {
  if (!m || len <= 0) return;
  const uint8_t status = m[0];
  if (status == 0xF0) {
    src = Source::SysEx;
    tx = false;
    sysexReceived(m, len);
    return;
  }
  if (status < 0x80 || status >= 0xF0) return;  // system common / realtime: ignored
  const int ch = status & 0x0F;
  if (!ps.globals[int(Global::Omni)] && ch != ps.globals[int(Global::MidiChannel)] - 1) return;  // R-M1 channel filter
  src = Source::Midi;
  tx = false;
  const int d1 = len > 1 ? (m[1] & 0x7F) : 0;
  const int d2 = len > 2 ? (m[2] & 0x7F) : 0;
  switch (status & 0xF0) {
    case 0x80:
      if (len >= 3) noteOff(d1, Source::Midi);
      break;
    case 0x90:
      if (len >= 3) {
        if (d2 == 0) noteOff(d1, Source::Midi);
        else noteOn(d1, d2, Source::Midi);
      }
      break;
    case 0xB0:
      if (len >= 3) controller(d1, d2, true, Source::Midi);
      break;
    case 0xC0:
      if (len >= 2) programChange(d1);
      break;
    case 0xD0:
      if (len >= 2) {
        aftertouchV = d1;
        engine.aftertouch(d1);
      }
      break;
    case 0xE0:
      if (len >= 3) {
        bend = d1 | (d2 << 7);
        engine.pitchBend(bend);
        pushWheels();
      }
      break;
    default: break;  // polyphonic aftertouch: not used
  }
}

void Core::Impl::noteOn(int note, int vel, Source s) {
  if (note < 0 || note > 127) return;
  if (vel <= 0) {
    noteOff(note, s);
    return;
  }
  if (vel > 127) vel = 127;
  // F-5: an armed KEY TRANSPOSE page eats the next note-on from any source.
  if (armed && mode == kModeEdit && editPage == 31 && !comparing && !splash) {
    armed = false;
    swallowed[note] = 1;
    src = Source::Panel;
    tx = true;
    int t = note - 36;
    writeVoice(kTranspose, t < 0 ? 0 : (t > 48 ? 48 : t));
    return;
  }
  engine.noteOn(note, vel);
  ++noteOnsToEngine;
  lastEngineNote = note;
  lastEngineVelocity = vel;
  if (s == Source::Keyboard && kbNotes[note] < 255) ++kbNotes[note];
}

void Core::Impl::noteOff(int note, Source s) {
  if (note < 0 || note > 127) return;
  if (swallowed[note]) {
    swallowed[note] = 0;
    return;
  }
  engine.noteOff(note);
  ++noteOffsToEngine;
  if (s == Source::Keyboard && kbNotes[note] > 0) --kbNotes[note];
}

void Core::Impl::releaseAll() {
  for (int n = 0; n < 128; ++n) {
    while (kbNotes[n] > 0) {
      engine.noteOff(n);
      --kbNotes[n];
    }
  }
  if (kbSustain) {
    engine.sustain(false);
    kbSustain = false;
    sustainOn = false;
  }
  bend = 8192;
  engine.pitchBend(bend);
  pushWheels();
}

void Core::Impl::controller(int cc, int v, bool fromMidi, Source s) {
  if (v < 0) v = 0;
  if (v > 127) v = 127;
  switch (cc) {
    case 1:
      modWheel = v;
      engine.modWheel(v);
      if (fromMidi) pushWheels();
      break;
    case 2:
      breathV = v;
      engine.breath(v);
      break;
    case 4:
      footV = v;
      engine.foot(v);
      break;
    case 5:  // portamento time, as an external change (R-S2)
      src = s;
      tx = false;
      externalFn(kFnPortaTime, (v * 198 + 127) / 254);
      break;
    case 6:  // data entry: acts as the slider at v/127
      src = Source::Panel;
      tx = !fromMidi;
      dataEntry(float(v) / 127.f);
      break;
    case 7: setGlobal(int(Global::Volume), v); break;
    case 64:
      sustainOn = v >= 64;
      engine.sustain(sustainOn);
      if (s == Source::Keyboard) kbSustain = sustainOn;
      break;
    case 65:
      portaOn = v >= 64;
      engine.portamentoSwitch(portaOn);
      break;
    case 96:
    case 97:  // YES/+1, NO/-1 (one step, no auto-repeat)
      if (splash) break;
      buttonDown(cc == 96 ? kBtnYes : kBtnNo, false);
      src = Source::Panel;
      tx = !fromMidi && tx;
      break;
    case 120:
      ++allSoundOffs;
      engine.allSoundOff();
      break;
    case 123:
      ++allNotesOffs;
      engine.allNotesOff();
      break;
    default: break;
  }
}

void Core::Impl::pushWheels() {
  Event e;
  e.type = EvType::Wheels;
  e.src = src;
  e.a = bend;
  e.b = modWheel;
  push(e);
}

// F-13.
void Core::Impl::sysexReceived(const uint8_t* m, int len) {
  sysex::Parsed p;
  if (!sysex::parseMessage(m, len, p)) return;
  if (!sysex::acceptReceived(p, ps.globals[int(Global::SysInfo)] != 0, ps.globals[int(Global::Omni)] != 0,
                             ps.globals[int(Global::MidiChannel)]))
    return;
  switch (p.kind) {
    case sysex::MsgKind::Voice: {
      cancelDialog();
      endNaming();
      sysex::Voice v;
      sysex::voiceFromUnpacked(p.data, v);
      loadEditBuffer(v, true);
      showMessage(p.checksumOk ? Msg::MidiReceived : Msg::ChecksumError);
      notice(kNoticeDumpReceived, 0);
      break;
    }
    case sysex::MsgKind::Bank:
      cancelDialog();
      if (bankProtected(kBankInt)) {
        showMessage(Msg::Protected);
        notice(kNoticeProtected, kBankInt);
        break;
      }
      std::memcpy(ps.intBank, p.data, sysex::kPackedBankBytes);
      showMessage(p.checksumOk ? Msg::MidiReceived : Msg::ChecksumError);
      notice(kNoticeDumpReceived, 1);
      break;
    case sysex::MsgKind::ParamChange:
      if (p.group == 0 && p.param <= kOpMask) externalVoice(p.param, p.value);
      else if (p.group == 2 && p.param >= kFnFirst && p.param <= kFnLast) externalFn(p.param, p.value);
      break;
    case sysex::MsgKind::DumpRequest:
      if (p.format == 0) sendMidi(msgBuf, sysex::buildVoiceDump(ps.edit.raw, 0, msgBuf));
      else if (p.format == 9) sendMidi(msgBuf, sysex::buildBankDump(ps.intBank, 0, msgBuf));
      break;
    default: break;
  }
}

void Core::Impl::sendMidi(const uint8_t* msg, int len) {
  if (len > 0) out->send(msg, len);
}

// ---- transmit throttle: at most one message per parameter every 10 ms, always the final value.

void Core::Impl::queueTx(int id) {
  if (!ps.globals[int(Global::SysInfo)] || id < 0 || id >= kTxIds) return;
  if (!txEver[id] || now - lastTx[id] >= tThrottle) sendParam(id);
  else txPending[id] = true;
}

void Core::Impl::sendParam(int id) {
  uint8_t m[sysex::kParamMsgBytes];
  if (id < 156) {
    int v;
    if (id == kOpMask) v = opMask;
    else if (id >= kName) v = ps.edit.raw[id] & 0x7F;
    else v = effective(id, ps.edit.raw[id]);
    sysex::buildParamChange(0, 0, id, v, m);
  } else {
    const int n = kFnFirst + (id - 156);
    sysex::buildParamChange(0, 2, n, ps.fn[n - kFnFirst], m);
  }
  sendMidi(m, sysex::kParamMsgBytes);
  lastTx[id] = now;
  txEver[id] = true;
  txPending[id] = false;
}

void Core::Impl::flushTx() {
  if (!ps.globals[int(Global::SysInfo)]) return;
  for (int id = 0; id < kTxIds; ++id)
    if (txPending[id] && now - lastTx[id] >= tThrottle) sendParam(id);
}

}  // namespace vdx

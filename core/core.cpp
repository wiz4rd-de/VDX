// vdx::Core: API, command dispatch, first-run state and the state blob.
// The front-panel state machine lives in core/panel/*.cpp (see core_impl.h).
#include "core.h"

#include <cmath>
#include <cstring>

#include "panel/core_impl.h"
#include "sysex/default_bank.h"
#include "sysex/state.h"
#include "sysex/sysex.h"

namespace vdx {

Core::Impl::Impl(EventSink& e, MidiOutSink& m) : ev(&e), out(&m) {
  firstRun();
  resetPanel();
  setTimers();
  syncShadows();
}

void Core::Impl::firstRun() {
  std::memset(&ps, 0, sizeof ps);
  std::memcpy(ps.intBank, sysex::defaultBankPacked(), sysex::kPackedBankBytes);
  sysex::unpackVoice(ps.intBank, ps.edit);  // internal slot 1 = INIT VOICE
  ps.stored = ps.edit;
  ps.recall = ps.edit;
  ps.edited = 0;
  ps.srcBank = kBankInt;
  ps.srcSlot = 1;
  for (int i = 0; i < state::kFunctionCount; ++i) ps.fn[i] = uint8_t(params::functionDefault(64 + i));
  ps.selMem = kBankInt;
  ps.globals[int(Global::MasterTune)] = params::kMasterTuneDefault;
  ps.globals[int(Global::MidiChannel)] = params::kMidiChannelDefault;
  ps.globals[int(Global::Omni)] = 1;
  ps.globals[int(Global::SysInfo)] = 0;
  ps.globals[int(Global::ProtectInt)] = 1;
  ps.globals[int(Global::ProtectCrt)] = 1;
  ps.globals[int(Global::Volume)] = params::kVolumeDefault;
  ps.settings[int(SettingId::Engine)] = 0;
  ps.settings[int(SettingId::Dac)] = 1;
  ps.settings[int(SettingId::HostRate)] = 0;
  ps.settings[int(SettingId::ClassicVelocity)] = 0;
  ps.settings[int(SettingId::KeyboardPlay)] = 1;
  ps.settings[int(SettingId::Zoom)] = 0;
  ps.settings[int(SettingId::OpOrder)] = 0;
  opMask = 0x3F;
  comparing = false;
  applySettingsStruct();
}

void Core::Impl::resetPanel() {
  mode = kModePlay;
  editPage = 7;
  fnPage = 1;
  selOp = 1;
  egStage = 0;
  pegStage = 0;
  scaleSide = 0;
  b17Sub = 0;
  b8Sub = 0;
  nameCursor = 0;
  comparing = false;
  armed = false;
  dialog = Dialog::None;
  msg = Msg::None;
  held = 0;
}

void Core::Impl::setTimers() {
  auto samples = [this](double seconds) { return int64_t(std::llround(seconds * rate)); };
  tSplash = samples(1.5);
  tMsg = samples(2.0);
  tRepeatDelay = samples(0.5);
  tRepeatPeriod = samples(0.1);
  tThrottle = samples(0.01);
  tMeter = int64_t(std::ceil(rate / 30.0));
}

void Core::Impl::applySettingsStruct() {
  settings.engine = ps.settings[int(SettingId::Engine)] ? EngineMode::Modern : EngineMode::MkI;
  settings.dac = ps.settings[int(SettingId::Dac)] != 0;
  settings.hostRate = ps.settings[int(SettingId::HostRate)] != 0;
  settings.classicVelocity = ps.settings[int(SettingId::ClassicVelocity)] != 0;
}

void Core::Impl::onCommand(const Command& c, const uint8_t* payload, int len) {
  src = c.src;
  tx = false;
  switch (c.type) {
    case CmdType::NoteOn: noteOn(c.a, c.b, c.src); break;
    case CmdType::NoteOff: noteOff(c.a, c.src); break;
    case CmdType::Controller: controller(c.a, c.b, false, c.src); break;
    case CmdType::PitchBend:
      bend = c.a < 0 ? 0 : (c.a > 16383 ? 16383 : c.a);
      engine.pitchBend(bend);
      break;
    case CmdType::MidiBytes: break;  // parsed by Instrument, delivered via midiMessage()
    case CmdType::ReleaseAll: releaseAll(); break;
    case CmdType::Button:
      if (c.b) buttonDown(c.a, true);
      else buttonUp(c.a);
      break;
    case CmdType::DataEntry:
      src = Source::Panel;
      tx = true;
      dataEntry(c.f);
      break;
    case CmdType::SetVoiceParam:
      if (c.src == Source::Programmer) programmerVoice(c.a, c.b);
      else if (c.src == Source::Panel) panelVoice(c.a, c.b);
      else externalVoice(c.a, c.b);
      break;
    case CmdType::SetFunctionParam:
      if (c.src == Source::Programmer) programmerFn(c.a, c.b);
      else if (c.src == Source::Panel) {
        tx = true;
        writeFn(c.a, c.b);
      } else externalFn(c.a, c.b);
      break;
    case CmdType::SetGlobal:
      if (c.src == Source::Programmer) {
        cancelDialog();
        endNaming();
      }
      setGlobal(c.a, c.b);
      break;
    case CmdType::SetSetting: setSetting(c.a, c.b); break;
    case CmdType::WriteOperator: writeOperator(c.a, c.data); break;
    case CmdType::SwapOperators: swapOperators(c.a, c.b); break;
    case CmdType::EgCopy:
      touchProgrammer();
      tx = c.src == Source::Programmer || c.src == Source::Panel;
      doEgCopy(c.a, c.b);
      break;
    case CmdType::InitVoice: initVoiceCmd(); break;
    case CmdType::Randomise: randomise(c.a); break;
    case CmdType::ToggleCompare: toggleCompare(); break;
    case CmdType::SelectVoice:
      cancelDialog();
      selectVoice(c.a, c.b, true);
      break;
    case CmdType::ProgramChange: programChange(c.a); break;
    case CmdType::CopyVoice: copyVoice(c.a, c.b, c.c, c.data[0]); break;
    case CmdType::LoadVoice: loadVoiceFile(payload, len, c.b != 0); break;
    case CmdType::LoadBank: loadBankFile(c.a, c.b != 0, payload, len); break;
    case CmdType::FileRejected: showMessage(Msg::FormatConflict); break;
    case CmdType::NewCartridge: newCartridge(); break;
    case CmdType::EjectCartridge: ejectCartridge(); break;
    case CmdType::Export: exportData(c.a); break;
    case CmdType::ClearCartridgeChanged:
      if (ps.crtPresent) ps.crtChanged = 0;
      break;
    case CmdType::SendBank: sendBank(); break;
    case CmdType::RequestDump: {
      uint8_t m[sysex::kDumpReqBytes];
      sendMidi(m, sysex::buildDumpRequest(0, 9, m));
      break;
    }
    case CmdType::StartSplash: startSplash(); break;
    case CmdType::RequestSnapshot:
      refresh();
      emitSnapshot();
      return;
    default: break;
  }
  refresh();
}

// ---- Core API -----------------------------------------------------------------

Core::Core(EventSink& events, MidiOutSink& midiOut) : impl_(new Impl(events, midiOut)) {}
Core::~Core() { delete impl_; }

void Core::prepare(double engineRate) {
  Impl& d = *impl_;
  d.rate = engineRate > 0 ? engineRate : kInternalRate;
  d.engine.prepare(d.rate);
  d.setTimers();
  d.engineValid = false;
  for (int i = 0; i < 128; ++i) {
    d.kbNotes[i] = 0;
    d.swallowed[i] = 0;
  }
  d.kbSustain = false;
  d.held = 0;
}

double Core::engineRate() const { return impl_->rate; }

void Core::command(const Command& cmd, const uint8_t* payload, int len) { impl_->onCommand(cmd, payload, len); }

void Core::midiMessage(const uint8_t* msg, int len) {
  impl_->midiMessage(msg, len);
  impl_->refresh();
}

void Core::renderBlock(int32_t* dacTap, float* out) {
  Impl& d = *impl_;
  d.processTimers();
  d.refresh();
  d.engine.render(dacTap, out);
  d.meters();
  d.now += kBlock;
}

const Settings& Core::settings() const { return impl_->settings; }
int Core::masterVolume() const { return impl_->ps.globals[int(Global::Volume)]; }

size_t Core::serialize(uint8_t* dst, size_t cap) const { return state::write(impl_->ps, dst, cap); }

Core::LoadResult Core::deserialize(const uint8_t* data, size_t len) {
  Impl& d = *impl_;
  d.scratchPs = d.ps;
  const state::ReadResult r = state::read(data, len, d.scratchPs);
  d.src = Source::State;
  d.tx = false;
  for (int i = 0; i < Impl::kTxIds; ++i) d.txPending[i] = false;
  if (r == state::ReadResult::Ok || r == state::ReadResult::Newer) {
    d.ps = d.scratchPs;
    d.opMask = 0x3F;
    d.applySettingsStruct();
    d.resetPanel();
    d.stateResetPending = false;
    d.lastLoad = LoadResult::Ok;
  } else if (r == state::ReadResult::Empty) {
    d.firstRun();
    d.resetPanel();
    d.stateResetPending = false;
    d.lastLoad = LoadResult::Empty;
  } else {
    d.firstRun();
    d.resetPanel();
    d.lastLoad = LoadResult::Reset;
    d.stateResetPending = true;
    // Shown after the splash; if the splash has already run, show it now.
    if (d.splashDone && !d.splash) {
      d.stateResetPending = false;
      d.showMessage(Msg::StateReset);
    }
  }
  d.refresh();
  return d.lastLoad;
}

Core::LoadResult Core::validate(const uint8_t* data, size_t len) {
  switch (state::validate(data, len)) {
    case state::ReadResult::Ok:
    case state::ReadResult::Newer: return LoadResult::Ok;
    case state::ReadResult::Empty: return LoadResult::Empty;
    default: return LoadResult::Reset;
  }
}

bool Core::takeStateDirty() {
  const bool d = impl_->dirtyForInstrument;
  impl_->dirtyForInstrument = false;
  return d;
}

fm::Engine& Core::engine() { return impl_->engine; }
Core::Impl& Core::impl() { return *impl_; }
const Core::Impl& Core::impl() const { return *impl_; }

}  // namespace vdx

#include "processor.h"

#include <cmath>
#include <cstring>
#include <thread>

#include "editor.h"
#include "persistence.h"
#include "sysex/default_bank.h"
#include "sysex/state.h"
#include "sysex/sysex.h"
#include "sysex/voice.h"
#include "vdx/params.h"

namespace vdx::plugin {

namespace {

constexpr int kNoEcho = -1;
constexpr int kScratchSamples = 4096;
constexpr int kMidiOutBytes = 16384;
constexpr uint32_t kIdleAfterMs = 200;  // no processBlock for longer -> idle pump
constexpr int kIdlePeriodMs = 10;

std::string eventText(const vdx::Event& e) {
  size_t n = 0;
  while (n < sizeof e.text && e.text[n] != 0) ++n;
  return std::string(e.text, n);
}

juce::String trimmedName(const char* ten) {
  return juce::String(juce::CharPointer_UTF8(std::string(ten, 10).c_str())).trimEnd();
}

// Length of the MIDI message starting with status byte s (complete messages
// only: the core transmits whole messages).
int messageLength(const uint8_t* p, int avail) {
  const uint8_t s = p[0];
  if (s == 0xF0) {
    int j = 1;
    while (j < avail && p[j] != 0xF7) ++j;
    return j < avail ? j + 1 : avail;
  }
  if (s >= 0xF8) return 1;
  switch (s & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 3;
    case 0xC0: case 0xD0: return 2;
    default: break;
  }
  switch (s) {
    case 0xF1: case 0xF3: return 2;
    case 0xF2: return 3;
    default: return 1;
  }
}

}  // namespace

// Keeps the core alive in the standalone when no audio device is running
// (no processBlock for > 200 ms): renders silent blocks into scratch memory.
class VdxProcessor::IdlePump final : public juce::HighResolutionTimer {
 public:
  explicit IdlePump(VdxProcessor& p) : p_(p) {}
  ~IdlePump() override { stopTimer(); }
  void hiResTimerCallback() override {
    const uint32_t now = juce::Time::getMillisecondCounter();
    if (now - p_.lastProcessMs_.load(std::memory_order_relaxed) <= kIdleAfterMs) return;
    if (p_.busy_.exchange(true, std::memory_order_acquire)) return;  // never with the audio callback
    p_.beginBlock();
    p_.renderSilent(juce::jmax(1, int(std::lround(p_.instrument_->hostRate() * kIdlePeriodMs / 1000.0))));
    p_.collectMidiOut(nullptr);
    p_.busy_.store(false, std::memory_order_release);
  }

 private:
  VdxProcessor& p_;
};

// ==== construction ===========================================================

VdxProcessor::VdxProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      standalone_(wrapperType == wrapperType_Standalone),
      instrument_(std::make_unique<vdx::Instrument>(48000.0, 512)),
      scratch_(size_t(kScratchSamples), 0.f),
      midiOut_(size_t(kMidiOutBytes), 0) {
  bridge_ = std::make_unique<vdx::bridge::Bridge>(*instrument_);
  for (auto& e : echo_) e.store(kNoEcho, std::memory_order_relaxed);

  const auto& table = hostParams();
  jassert(int(table.size()) == kHostParamCount);
  for (int i = 0; i < kHostParamCount; ++i) {
    const HostParamInfo& info = table[size_t(i)];
    auto toText = [this, i](int v, int maxLen) {
      std::array<int, kHostParamCount> ctx{};
      for (int k = 0; k < kHostParamCount; ++k) ctx[size_t(k)] = hostValue(k);
      juce::String s(hostValueString(i, v, ctx.data()));
      return maxLen > 0 ? s.substring(0, maxLen) : s;
    };
    auto fromText = [this, i](const juce::String& t) {
      std::array<int, kHostParamCount> ctx{};
      for (int k = 0; k < kHostParamCount; ++k) ctx[size_t(k)] = hostValue(k);
      return hostValueFromString(i, t.toStdString(), ctx.data());
    };
    juce::RangedAudioParameter* p = nullptr;
    if (info.isBool()) {
      auto attrs = juce::AudioParameterBoolAttributes()
                       .withStringFromValueFunction([toText](bool v, int maxLen) { return toText(v ? 1 : 0, maxLen); })
                       .withValueFromStringFunction([fromText](const juce::String& t) { return fromText(t) != 0; });
      auto* b = new juce::AudioParameterBool(juce::ParameterID{info.id, 1}, info.name, info.def != 0, attrs);
      bools_.push_back(b);
      ints_.push_back(nullptr);
      p = b;
    } else {
      auto attrs = juce::AudioParameterIntAttributes()
                       .withStringFromValueFunction([toText](int v, int maxLen) { return toText(v, maxLen); })
                       .withValueFromStringFunction([fromText](const juce::String& t) { return fromText(t); });
      auto* n = new juce::AudioParameterInt(juce::ParameterID{info.id, 1}, info.name, 0, info.max, info.def, attrs);
      ints_.push_back(n);
      bools_.push_back(nullptr);
      p = n;
    }
    addParameter(p);
    params_.push_back(p);
    lastSeen_[size_t(i)] = info.def;
  }

  for (int s = 0; s < 32; ++s) {
    char name[10];
    vdx::sysex::packedName(vdx::sysex::defaultBankPacked() + s * vdx::sysex::kPackedVoiceBytes, name);
    programNames_[size_t(s)] = trimmedName(name);
  }

  if (!standalone_) {
    // §4.4: computer keyboard play defaults to off in the plugin. Applied
    // before any audio thread exists; a later setStateInformation overrides it.
    vdx::Command c;
    c.type = vdx::CmdType::SetSetting;
    c.src = vdx::Source::State;
    c.a = int(vdx::SettingId::KeyboardPlay);
    c.b = 0;
    instrument_->applyNow(c);
    // The saved-state snapshot is republished by render(): one silent block
    // makes a host save before any audio has run already hold this default.
    renderSilent(vdx::kBlock);
  }

  if (standalone_) {
    // F-1: load state.bin (or first-run state) before the window opens.
    persistence_ = std::make_unique<vdx::shell::Persistence>();
    std::vector<uint8_t> blob;
    if (persistence_->load(blob)) {
      vdx::StateBlob b;
      b.bytes = std::move(blob);
      instrument_->loadState(b);
    }
    pump_ = std::make_unique<IdlePump>(*this);
    pump_->startTimer(kIdlePeriodMs);
  }

  setLatencySamples(instrument_->latencySamples());
  startTimerHz(60);
}

VdxProcessor::~VdxProcessor() {
  stopTimer();
  pump_.reset();
  chooser_.reset();
}

juce::AudioProcessorEditor* VdxProcessor::createEditor() { return new VdxEditor(*this); }

// ==== audio thread ===========================================================

int VdxProcessor::hostValue(int i) const {
  if (auto* n = ints_[size_t(i)]) return n->get();
  return bools_[size_t(i)]->get() ? 1 : 0;
}

void VdxProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
  if (standalone_)
    while (busy_.exchange(true, std::memory_order_acquire)) std::this_thread::yield();
  instrument_->prepare(sampleRate, juce::jmax(1, maximumExpectedSamplesPerBlock));
  setLatencySamples(instrument_->latencySamples());
  if (standalone_) {
    lastProcessMs_.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    busy_.store(false, std::memory_order_release);
  }
}

bool VdxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  for (const auto& in : layouts.inputBuses)
    if (!in.isDisabled()) return false;
  const auto out = layouts.getMainOutputChannelSet();
  return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VdxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  const int n = buffer.getNumSamples();
  if (standalone_) {
    lastProcessMs_.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    if (busy_.exchange(true, std::memory_order_acquire)) {  // the idle pump is mid-block
      buffer.clear();
      midi.clear();
      return;
    }
  }

  beginBlock();
  for (const auto m : midi) instrument_->midi(m.data, m.numBytes, m.samplePosition);
  midi.clear();

  const int channels = buffer.getNumChannels();
  if (channels > 0 && n > 0) {
    float* left = buffer.getWritePointer(0);
    instrument_->render(left, nullptr, n);
    for (int c = 1; c < channels; ++c) buffer.copyFrom(c, 0, left, n);
  } else if (n > 0) {
    renderSilent(n);
  }
  collectMidiOut(&midi);

  if (keyboardDefaultPending_.exchange(false, std::memory_order_acq_rel)) {
    vdx::Command c;
    c.type = vdx::CmdType::SetSetting;
    c.src = vdx::Source::State;
    c.a = int(vdx::SettingId::KeyboardPlay);
    c.b = 0;
    instrument_->applyNow(c);
  }

  if (standalone_) busy_.store(false, std::memory_order_release);
}

void VdxProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  processBlock(buffer, midi);
  buffer.clear();
  midi.clear();
}

void VdxProcessor::beginBlock() { applyHostChanges(); }

void VdxProcessor::applyHostChanges() {
  // Host program first, so host parameter values written in the same block win.
  const int program = pendingProgram_.exchange(-1, std::memory_order_acq_rel);
  if (program >= 0) {
    vdx::Command c;
    c.type = vdx::CmdType::ProgramChange;
    c.src = vdx::Source::Midi;  // acts as MIDI program change; reported back to the host (R-S4)
    c.a = program;
    instrument_->applyNow(c);
    srcBank_.store(0);
    srcSlot_.store(program + 1);
  }

  const auto& table = hostParams();
  bool maskChanged = false;
  bool assignChanged[4] = {false, false, false, false};
  for (int i = 0; i < kHostParamCount; ++i) {
    const int v = hostValue(i);
    if (v == lastSeen_[size_t(i)]) continue;
    lastSeen_[size_t(i)] = v;
    const int echoed = echo_[size_t(i)].exchange(kNoEcho, std::memory_order_acq_rel);
    if (echoed == v) continue;  // the plugin's own report (R-S4): never applied again
    const HostParamInfo& info = table[size_t(i)];
    vdx::Command c;
    c.src = vdx::Source::Host;
    switch (info.kind) {
      case HostKind::Voice:
        c.type = vdx::CmdType::SetVoiceParam;
        c.a = info.index;
        c.b = v;
        instrument_->applyNow(c);
        break;
      case HostKind::Function:
        c.type = vdx::CmdType::SetFunctionParam;
        c.a = info.index;
        c.b = v;
        instrument_->applyNow(c);
        break;
      case HostKind::MasterTune:
        c.type = vdx::CmdType::SetGlobal;
        c.a = int(vdx::Global::MasterTune);
        c.b = v;
        instrument_->applyNow(c);
        break;
      case HostKind::OpSwitch: maskChanged = true; break;
      case HostKind::AssignBit: assignChanged[(info.index - vdx::params::kFnWheelAssign) / 2] = true; break;
    }
  }
  if (maskChanged) {
    int mask = 0;
    for (int op = 1; op <= 6; ++op)
      if (hostValue(hostIndexOfOpSwitch(op))) mask |= 1 << (6 - op);  // bit 5 = OP1 … bit 0 = OP6
    vdx::Command c;
    c.type = vdx::CmdType::SetVoiceParam;
    c.src = vdx::Source::Host;
    c.a = vdx::params::kOpMask;
    c.b = mask;
    instrument_->applyNow(c);
  }
  for (int s = 0; s < 4; ++s) {
    if (!assignChanged[s]) continue;
    const int fn = vdx::params::kFnWheelAssign + 2 * s;
    int value = 0;
    for (int b = 0; b < 3; ++b)
      if (hostValue(hostIndexOfAssignBit(fn, b))) value |= 1 << b;
    vdx::Command c;
    c.type = vdx::CmdType::SetFunctionParam;
    c.src = vdx::Source::Host;
    c.a = fn;
    c.b = value;
    instrument_->applyNow(c);
  }
}

void VdxProcessor::renderSilent(int numSamples) {
  while (numSamples > 0) {
    const int k = juce::jmin(numSamples, int(scratch_.size()));
    instrument_->render(scratch_.data(), nullptr, k);
    numSamples -= k;
  }
}

void VdxProcessor::collectMidiOut(juce::MidiBuffer* out) {
  for (int round = 0; round < 8; ++round) {
    const int len = instrument_->readMidiOut(midiOut_.data(), int(midiOut_.size()));
    if (len <= 0) break;
    if (out == nullptr) continue;
    int i = 0;
    while (i < len) {
      if (midiOut_[size_t(i)] < 0x80) {  // never expected: skip stray data bytes
        ++i;
        continue;
      }
      const int m = messageLength(midiOut_.data() + i, len - i);
      out->addEvent(midiOut_.data() + i, m, 0);
      i += m;
    }
  }
}

// ==== programs and state =====================================================

int VdxProcessor::getCurrentProgram() {
  const int pending = pendingProgram_.load(std::memory_order_acquire);
  if (pending >= 0) return pending;
  const int slot = srcSlot_.load();
  return srcBank_.load() == vdx::kBankInt && slot >= 1 && slot <= 32 ? slot - 1 : 0;
}

void VdxProcessor::setCurrentProgram(int index) {
  // Applied by the audio thread at the start of the next block (any calling thread).
  if (index >= 0 && index < 32) pendingProgram_.store(index, std::memory_order_release);
}

const juce::String VdxProcessor::getProgramName(int index) {
  if (index < 0 || index >= 32) return {};
  const juce::ScopedLock sl(namesLock_);
  return programNames_[size_t(index)];
}

void VdxProcessor::getStateInformation(juce::MemoryBlock& destData) {
  vdx::StateBlob b;
  instrument_->saveState(b);
  destData.replaceAll(b.bytes.data(), b.bytes.size());
}

void VdxProcessor::setStateInformation(const void* data, int sizeInBytes) {
  vdx::StateBlob b;
  if (data != nullptr && sizeInBytes > 0) {
    const auto* p = static_cast<const uint8_t*>(data);
    b.bytes.assign(p, p + sizeInBytes);
  }
  const auto r = vdx::state::validate(b.bytes.data(), b.bytes.size());
  instrument_->loadState(b);
  // An empty or unusable blob gives first-run state: keep the plugin default.
  if (!standalone_ && r != vdx::state::ReadResult::Ok && r != vdx::state::ReadResult::Newer)
    keyboardDefaultPending_.store(true, std::memory_order_release);
}

// ==== message thread =========================================================

void VdxProcessor::postCommand(vdx::CmdType type, vdx::Source src, int a, int b) {
  vdx::Command c;
  c.type = type;
  c.src = src;
  c.a = a;
  c.b = b;
  instrument_->post(c);
}

void VdxProcessor::reportToHost(int hostIndex, int value) {
  if (hostIndex < 0) return;
  pendingReport_[size_t(hostIndex)] = value;
  hasPendingReport_[size_t(hostIndex)] = true;
}

void VdxProcessor::drainCoreEvents() {
  using vdx::EvType;
  // The host learns the current program once at start (AU: present preset).
  bool programListChanged = !programReported_;
  programReported_ = true;
  std::string batch;
  vdx::Event e;
  while (instrument_->pollEvent(e)) {
    if (e.type == EvType::ExportData) {
      std::vector<uint8_t> bytes(e.len);
      const int n = instrument_->readPayload(e, bytes.data(), int(bytes.size()));
      bytes.resize(size_t(juce::jmax(0, n)));
      if (eventObserver) eventObserver(e);
      exportFile(vdx::ExportKind(e.a), eventText(e), std::move(bytes));
      continue;
    }
    if (e.slot != 0xFFFFFFFFu) instrument_->readPayload(e, nullptr, 0);
    if (eventObserver) eventObserver(e);

    const bool fromHost = e.src == vdx::Source::Host;
    switch (e.type) {
      case EvType::VoiceParam:
        if (fromHost) break;
        if (e.a == vdx::params::kOpMask) {
          for (int op = 1; op <= 6; ++op) reportToHost(hostIndexOfOpSwitch(op), (e.b >> (6 - op)) & 1);
        } else {
          reportToHost(hostIndexOfVoice(e.a), e.b);
        }
        break;
      case EvType::FunctionParam:
        if (fromHost) break;
        if (hostIndexOfFunction(e.a) >= 0) {
          reportToHost(hostIndexOfFunction(e.a), e.b);
        } else {
          for (int b = 0; b < 3; ++b) reportToHost(hostIndexOfAssignBit(e.a, b), (e.b >> b) & 1);
        }
        break;
      case EvType::GlobalValue:
        if (!fromHost && e.a == int(vdx::Global::MasterTune)) reportToHost(hostIndexOfMasterTune(), e.b);
        break;
      case EvType::SettingValue:
        if (e.a == int(vdx::SettingId::Zoom) && e.b != zoom_) {
          zoom_ = e.b;
          if (editor_ != nullptr) editor_->zoomSettingChanged(zoom_);
        }
        break;
      case EvType::VoiceName:
        if (e.a == vdx::kBankInt && e.b >= 1 && e.b <= 32) {
          const juce::String name = trimmedName(e.text);
          const juce::ScopedLock sl(namesLock_);
          if (programNames_[size_t(e.b - 1)] != name) {
            programNames_[size_t(e.b - 1)] = name;
            programListChanged = true;
          }
        }
        break;
      case EvType::Memory: {
        const int before = getCurrentProgram();
        srcBank_.store(e.b);
        srcSlot_.store(e.c);
        if (getCurrentProgram() != before) programListChanged = true;
        break;
      }
      case EvType::StateDirty:
        if (persistence_) persistence_->markChanged(int64_t(juce::Time::getMillisecondCounter()));
        continue;  // not a UI event
      default: break;
    }
    if (editor_ != nullptr && uiAlive_) {
      const std::string js = vdx::bridge::encodeEvent(e);
      if (!js.empty()) {
        batch += batch.empty() ? "[" : ",";
        batch += js;
      }
    }
  }

  // R-S4: one notification per changed host parameter, the last value of the
  // batch. The echo is stored first so the audio thread never re-applies it.
  for (int i = 0; i < kHostParamCount; ++i) {
    if (!hasPendingReport_[size_t(i)]) continue;
    hasPendingReport_[size_t(i)] = false;
    const int v = pendingReport_[size_t(i)];
    if (hostValue(i) == v) continue;
    echo_[size_t(i)].store(v, std::memory_order_release);
    params_[size_t(i)]->setValueNotifyingHost(params_[size_t(i)]->convertTo0to1(float(v)));
    ++hostReports_;
  }

  if (!batch.empty()) {
    batch += "]";
    sendToUi(batch);
  }
  if (programListChanged) updateHostDisplay(ChangeDetails().withProgramChanged(true));

  const int latency = instrument_->latencySamples();
  if (latency != getLatencySamples()) setLatencySamples(latency);

  // Events were lost (queue full): resynchronise host parameters and UI.
  const uint32_t dropped = instrument_->droppedEvents();
  if (dropped != droppedSeen_) {
    droppedSeen_ = dropped;
    postCommand(vdx::CmdType::RequestSnapshot, vdx::Source::Internal);
  }

  maybeSaveStandaloneState(false);
}

std::string VdxProcessor::envJson() const {
  std::string s = "{\"e\":\"env\",\"build\":";
  s += standalone_ ? "\"standalone\"" : "\"plugin\"";
  const bool midiOk = !standalone_ || !hooks_.midiOutputAvailable || hooks_.midiOutputAvailable();
  s += ",\"midi\":";
  s += midiOk ? "\"ok\"" : "\"unavailable\"";
  const bool audio = !standalone_ || !hooks_.audioRunning || hooks_.audioRunning();
  s += ",\"audio\":";
  s += audio ? "true" : "false";
  s += "}";
  return s;
}

void VdxProcessor::sendEnvToUi() {
  if (uiAlive_) sendToUi("[" + envJson() + "]");
}

std::string VdxProcessor::handleUiCommand(const std::string& json) {
  vdx::bridge::Json j;
  if (!vdx::bridge::parseJson(json, j) || j.type != vdx::bridge::Json::Type::Object) return {};
  const std::string t = j.str("t");
  uiAlive_ = true;  // the page is loaded and talks to us
  if (t == "uiReady") {
    if (!splashStarted_) {  // F-1: the splash runs once per instance
      splashStarted_ = true;
      postCommand(vdx::CmdType::StartSplash, vdx::Source::Internal);
    }
    std::string out = "[" + envJson();
    if (!lastSaveError_.empty())
      out += ",{\"e\":\"notice\",\"code\":\"saveError\",\"text\":" + vdx::bridge::quote(lastSaveError_) + "}";
    return out + "]";
  }
  if (t == "pickFile") {
    pickFile();
    return {};
  }
  if (t == "openDevices") {
    if (hooks_.openDevices) hooks_.openDevices();
    return {};
  }
  if (t == "editorClosing") {
    postCommand(vdx::CmdType::ReleaseAll, vdx::Source::Keyboard);
    uiAlive_ = false;  // the page is going away
    return {};
  }
  if (vdx::bridge::Bridge::isShellCommand(t)) return {};  // midiSelect: web only
  const std::string reply = bridge_->handle(json);
  return reply == "[]" ? std::string() : reply;
}

void VdxProcessor::editorOpened(VdxEditor* editor) {
  editor_ = editor;
  uiAlive_ = false;
}

void VdxProcessor::editorClosed(VdxEditor* editor) {
  if (editor_ != editor) return;
  editor_ = nullptr;
  uiAlive_ = false;
  // F-15: closing the editor releases keyboard notes and centres the pitch wheel.
  postCommand(vdx::CmdType::ReleaseAll, vdx::Source::Keyboard);
}

void VdxProcessor::pageNavigating() { uiAlive_ = false; }

void VdxProcessor::sendToUi(const std::string& jsonArray) {
  if (editor_ != nullptr && uiAlive_ && !jsonArray.empty()) editor_->sendEvents(jsonArray);
}

void VdxProcessor::sendNotice(const std::string& code, const std::string& text) {
  sendToUi("[{\"e\":\"notice\",\"code\":" + vdx::bridge::quote(code) + ",\"text\":" + vdx::bridge::quote(text) + "}]");
}

void VdxProcessor::pickFile() {
  if (chooserActive_) return;
  chooserActive_ = true;
  // The filter description (§5.7, R-L5); the native dialogs show it as the title.
  chooser_ = std::make_unique<juce::FileChooser>(
      "DX7 SysEx files", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this](const juce::FileChooser& fc) {
                          chooserActive_ = false;
                          const juce::File f = fc.getResult();
                          if (f == juce::File()) return;  // cancelled: nothing changes
                          std::vector<uint8_t> bytes;
                          // R-D4: files larger than 1 MiB are rejected; read at most one byte more.
                          if (auto in = f.createInputStream()) {
                            const juce::int64 limit = juce::int64(vdx::sysex::kMaxFileBytes) + 1;
                            const juce::int64 n = juce::jmin<juce::int64>(in->getTotalLength(), limit);
                            bytes.resize(size_t(juce::jmax<juce::int64>(0, n)));
                            if (!bytes.empty()) bytes.resize(size_t(juce::jmax(0, in->read(bytes.data(), int(bytes.size())))));
                          }
                          const std::string reply = bridge_->importFile(f.getFileName().toStdString(), bytes);
                          sendToUi(reply);
                        });
}

void VdxProcessor::exportFile(vdx::ExportKind kind, const std::string& name, std::vector<uint8_t> bytes) {
  if (chooserActive_) return;  // one file dialog at a time
  chooserActive_ = true;
  const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
  chooser_ = std::make_unique<juce::FileChooser>("Export", dir.getChildFile(juce::String::fromUTF8(name.c_str())), "*.syx");
  auto data = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, kind, data](const juce::FileChooser& fc) {
                          chooserActive_ = false;
                          const juce::File f = fc.getResult();
                          if (f == juce::File()) return;
                          const bool ok = f.replaceWithData(data->data(), data->size());
                          if (ok) {
                            // F-12.5: exporting the cartridge clears its changed flag.
                            if (kind == vdx::ExportKind::Cartridge)
                              postCommand(vdx::CmdType::ClearCartridgeChanged, vdx::Source::BankManager);
                          } else if (standalone_) {
                            sendNotice("writeError", "Could not write " + f.getFullPathName().toStdString());
                          }
                        });
}

void VdxProcessor::maybeSaveStandaloneState(bool force) {
  if (!persistence_) return;
  const auto now = int64_t(juce::Time::getMillisecondCounter());
  if (!force && !persistence_->saveDue(now)) return;
  vdx::StateBlob b;
  instrument_->saveState(b);
  const vdx::shell::SaveResult r = persistence_->save(b.bytes);
  if (r.ok) {
    lastSaveError_.clear();
  } else {
    lastSaveError_ = r.error;  // F-15: VDX keeps running and shows the reason
    sendNotice("saveError", r.error);
  }
}

void VdxProcessor::saveStandaloneStateNow() { maybeSaveStandaloneState(true); }

}  // namespace vdx::plugin

// JUCE entry point for every wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vdx::plugin::VdxProcessor(); }

// Plugin acceptance tests (SPEC §13.4): the real VdxProcessor driven
// headlessly — processBlock on a test "audio thread" (here: the test thread
// unless stated), drainCoreEvents() standing in for the 60 Hz timer.
#include <catch_amalgamated.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <thread>

#include "host_params.h"
#include "panel/format.h"
#include "processor.h"
#include "sysex/default_bank.h"
#include "sysex/voice.h"
#include "vdx/params.h"

using vdx::plugin::HostKind;
using vdx::plugin::VdxProcessor;
namespace params = vdx::params;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

juce::RangedAudioParameter* ranged(VdxProcessor& p, int i) {
  return dynamic_cast<juce::RangedAudioParameter*>(p.getParameters()[i]);
}

int hostValue(VdxProcessor& p, int i) {
  auto* r = ranged(p, i);
  return juce::roundToInt(r->convertFrom0to1(r->getValue()));
}

std::string paramId(VdxProcessor& p, int i) {
  return dynamic_cast<juce::AudioProcessorParameterWithID*>(p.getParameters()[i])->getParameterID().toStdString();
}

std::string text(VdxProcessor& p, int i, int value) {
  auto* r = ranged(p, i);
  return r->getText(r->convertTo0to1(float(value)), 64).toStdString();
}

int indexOf(VdxProcessor& p, const std::string& id) {
  for (int i = 0; i < p.getParameters().size(); ++i)
    if (paramId(p, i) == id) return i;
  return -1;
}

bool isParamEvent(const vdx::Event& e) {
  return e.type == vdx::EvType::VoiceParam || e.type == vdx::EvType::FunctionParam ||
         (e.type == vdx::EvType::GlobalValue && e.a == int(vdx::Global::MasterTune));
}

// One processor plus its buffers and an event log.
struct Rig {
  std::unique_ptr<VdxProcessor> p = std::make_unique<VdxProcessor>();
  juce::AudioBuffer<float> buffer{2, kBlock};
  juce::MidiBuffer midi;
  std::vector<vdx::Event> events;

  Rig() {
    p->prepareToPlay(kRate, kBlock);
    p->eventObserver = [this](const vdx::Event& e) { events.push_back(e); };
  }
  void block(const juce::MidiBuffer* in = nullptr) {
    buffer.clear();
    midi.clear();
    if (in) midi = *in;
    p->processBlock(buffer, midi);
  }
  void drain() { p->drainCoreEvents(); }
  void settle(int blocks = 8) {
    for (int i = 0; i < blocks; ++i) {
      block();
      drain();
    }
    events.clear();
  }
  void post(vdx::CmdType t, vdx::Source s, int a = 0, int b = 0) {
    vdx::Command c;
    c.type = t;
    c.src = s;
    c.a = a;
    c.b = b;
    p->instrument().post(c);
  }
};

// Effective voice values of default-bank slot 1..32.
std::array<uint8_t, 155> defaultVoice(int slot) {
  vdx::sysex::Voice v;
  vdx::sysex::unpackVoice(vdx::sysex::defaultBankPacked() + (slot - 1) * vdx::sysex::kPackedVoiceBytes, v);
  std::array<uint8_t, 155> eff{};
  vdx::sysex::effectiveVoice(v.raw, eff.data());
  return eff;
}

struct CountingListener final : juce::AudioProcessorListener {
  std::map<int, int> counts;
  int total = 0;
  void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override {
    ++counts[index];
    ++total;
  }
  void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
};

}  // namespace

// ---- AC-PL2 --------------------------------------------------------------------

TEST_CASE("AC-PL2 host parameters: 174 with the 4.9 IDs, ranges, names, order and value strings; 32 programs") {
  VdxProcessor p;
  const auto& ps = p.getParameters();
  REQUIRE(ps.size() == 174);

  // Order and IDs: OP1..OP6 blocks (switch, offsets 0..20), globals, function, master tune.
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::vector<int> maxima;
  std::vector<bool> bools;
  const int opMax[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
  for (int op = 1; op <= 6; ++op) {
    const std::string n = std::to_string(op);
    ids.push_back("op" + n + "_on");
    names.push_back("OP" + n + " On");
    maxima.push_back(1);
    bools.push_back(true);
    for (int k = 0; k < 21; ++k) {
      ids.push_back("op" + n + "_" + params::kOpParamId[k]);
      names.push_back("OP" + n + " " + params::kOpParamName[k]);
      maxima.push_back(opMax[k]);
      bools.push_back(false);
    }
  }
  const int globalMax[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
  for (int g = 0; g < 19; ++g) {
    ids.push_back(params::kGlobalParamId[g]);
    names.push_back(params::kGlobalParamName[g]);
    maxima.push_back(globalMax[g]);
    bools.push_back(false);
  }
  const char* fnIds[6] = {"fn_mono", "fn_bend_range", "fn_bend_step", "fn_porta_mode", "fn_glissando", "fn_porta_time"};
  const char* fnNames[6] = {"Mono/Poly", "Pitch Bend Range", "Pitch Bend Step", "Portamento Mode", "Glissando",
                            "Portamento Time"};
  const int fnMax[6] = {1, 12, 12, 1, 1, 99};
  for (int k = 0; k < 6; ++k) {
    ids.push_back(fnIds[k]);
    names.push_back(fnNames[k]);
    maxima.push_back(fnMax[k]);
    bools.push_back(false);
  }
  const char* src[4] = {"wheel", "foot", "breath", "after"};
  const char* srcName[4] = {"Wheel", "Foot", "Breath", "Aftertouch"};
  for (int s = 0; s < 4; ++s) {
    ids.push_back(std::string(src[s]) + "_range");
    names.push_back(std::string(srcName[s]) + " Range");
    maxima.push_back(99);
    bools.push_back(false);
    const char* bit[3] = {"pitch", "amp", "egbias"};
    const char* bitName[3] = {"Pitch", "Amp", "EG Bias"};
    for (int b = 0; b < 3; ++b) {
      ids.push_back(std::string(src[s]) + "_" + bit[b]);
      names.push_back(std::string(srcName[s]) + " " + bitName[b]);
      maxima.push_back(1);
      bools.push_back(true);
    }
  }
  ids.push_back("master_tune");
  names.push_back("Master Tune");
  maxima.push_back(127);
  bools.push_back(false);
  REQUIRE(ids.size() == 174);
  REQUIRE(std::set<std::string>(ids.begin(), ids.end()).size() == 174);

  int voiceCount = 0, fnCount = 0;
  for (int i = 0; i < 174; ++i) {
    INFO("host parameter " << i << " " << ids[size_t(i)]);
    CHECK(paramId(p, i) == ids[size_t(i)]);
    CHECK(ps[i]->getName(64).toStdString() == names[size_t(i)]);
    if (bools[size_t(i)]) {
      CHECK(dynamic_cast<juce::AudioParameterBool*>(ps[i]) != nullptr);
    } else {
      auto* n = dynamic_cast<juce::AudioParameterInt*>(ps[i]);
      REQUIRE(n != nullptr);
      CHECK(n->getRange().getStart() == 0);
      CHECK(n->getRange().getEnd() == maxima[size_t(i)]);
    }
    CHECK(ps[i]->isAutomatable());
    if (i < 151) ++voiceCount;
    else if (i < 173) ++fnCount;
  }
  CHECK(voiceCount == 151);
  CHECK(fnCount == 22);

  // Value strings equal the LCD/programmer strings (R-P8): spec examples.
  CHECK(text(p, indexOf(p, "g_algorithm"), 0) == "1");
  CHECK(text(p, indexOf(p, "g_algorithm"), 31) == "32");
  CHECK(text(p, indexOf(p, "op2_detune"), 0) == "-7");
  CHECK(text(p, indexOf(p, "op2_detune"), 7) == "0");
  CHECK(text(p, indexOf(p, "op2_detune"), 14) == "+7");
  CHECK(text(p, indexOf(p, "g_transpose"), 0) == "C 1");
  CHECK(text(p, indexOf(p, "g_transpose"), 24) == "C 3");
  CHECK(text(p, indexOf(p, "g_transpose"), 48) == "C 5");
  CHECK(text(p, indexOf(p, "op4_break_point"), 0) == "A -1");
  CHECK(text(p, indexOf(p, "op4_break_point"), 39) == "C 3");
  CHECK(text(p, indexOf(p, "op4_break_point"), 99) == "C 8");
  const char* waves[6] = {"TRIANGL", "SAW DWN", "SAW UP", "SQUARE", "SINE", "S/HOLD"};
  for (int w = 0; w < 6; ++w) CHECK(text(p, indexOf(p, "g_lfo_wave"), w) == waves[w]);
  const char* curves[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
  for (int c = 0; c < 4; ++c) CHECK(text(p, indexOf(p, "op1_right_curve"), c) == curves[c]);
  CHECK(text(p, indexOf(p, "op1_on"), 0) == "OFF");
  CHECK(text(p, indexOf(p, "op1_on"), 1) == "ON");
  CHECK(text(p, indexOf(p, "g_lfo_key_sync"), 1) == "ON");
  CHECK(text(p, indexOf(p, "g_osc_key_sync"), 0) == "OFF");
  CHECK(text(p, indexOf(p, "wheel_egbias"), 1) == "ON");
  CHECK(text(p, indexOf(p, "foot_amp"), 0) == "OFF");
  CHECK(text(p, indexOf(p, "master_tune"), 0) == "-75.0 cents");
  CHECK(text(p, indexOf(p, "master_tune"), 64) == "0.0 cents");
  CHECK(text(p, indexOf(p, "master_tune"), 127) == "+73.8 cents");
  CHECK(text(p, indexOf(p, "fn_mono"), 0) == "POLY MODE");
  CHECK(text(p, indexOf(p, "fn_mono"), 1) == "MONO MODE");
  CHECK(text(p, indexOf(p, "fn_porta_mode"), 0) == "SUS-KEY P RETAIN");
  CHECK(text(p, indexOf(p, "fn_glissando"), 1) == "ON");
  CHECK(text(p, indexOf(p, "fn_bend_range"), 2) == "2");
  CHECK(text(p, indexOf(p, "op3_osc_mode"), 0) == "FREQUENCY(RATIO)");
  CHECK(text(p, indexOf(p, "op3_osc_mode"), 1) == "FIXED FREQ.(Hz)");
  CHECK(text(p, indexOf(p, "op3_coarse"), 0) == "0.50");
  CHECK(text(p, indexOf(p, "op3_coarse"), 1) == "1.00");
  CHECK(text(p, indexOf(p, "op3_coarse"), 31) == "31.00");
  CHECK(text(p, indexOf(p, "op1_output_level"), 99) == "99");

  // Every parameter at min and max: identical to the core formatting module
  // with the same context, and the text parses back to the value.
  const auto first = defaultVoice(1);
  uint8_t eff[156] = {};
  std::copy(first.begin(), first.end(), eff);
  eff[params::kOpMask] = 0x3F;
  uint8_t fn14[14];
  for (int n = 64; n <= 77; ++n) fn14[n - 64] = uint8_t(params::functionDefault(n));
  const auto& table = vdx::plugin::hostParams();
  for (int i = 0; i < 174; ++i) {
    const auto& info = table[size_t(i)];
    for (int v : {0, info.max}) {
      INFO("host parameter " << ids[size_t(i)] << " = " << v);
      char buf[vdx::fmt::kMaxText] = {};
      std::string expect;
      switch (info.kind) {
        case HostKind::Voice: vdx::fmt::voiceString(eff, info.index, v, buf); expect = buf; break;
        case HostKind::OpSwitch: expect = vdx::fmt::opOnString(v != 0); break;
        case HostKind::Function: vdx::fmt::functionString(fn14, info.index, v, buf); expect = buf; break;
        case HostKind::AssignBit: vdx::fmt::assignBitString(v << info.bit, info.bit, buf); expect = buf; break;
        case HostKind::MasterTune: vdx::fmt::globalString(int(vdx::Global::MasterTune), v, buf); expect = buf; break;
      }
      CHECK(text(p, i, v) == expect);
      auto* r = ranged(p, i);
      CHECK(juce::roundToInt(r->convertFrom0to1(r->getValueForText(juce::String(expect)))) == v);
    }
  }

  // 32 programs named after the internal voices (§15.3 default bank).
  REQUIRE(p.getNumPrograms() == 32);
  const char* bank[9] = {"INIT VOICE", "E.PIANO", "BELL", "BASS", "BRASS", "ORGAN", "MARIMBA", "STRINGS", "PAD"};
  for (int i = 0; i < 32; ++i) CHECK(p.getProgramName(i).toStdString() == (i < 9 ? bank[i] : "INIT VOICE"));
  CHECK(p.getCurrentProgram() == 0);
}

// ---- AC-PL3 --------------------------------------------------------------------

TEST_CASE("AC-PL3 state during playback round-trips into a new instance without real-time overruns") {
  Rig a;
  a.settle();
  // A non-default state: another voice, edits, a function parameter and settings.
  a.post(vdx::CmdType::ProgramChange, vdx::Source::Midi, 4);
  a.post(vdx::CmdType::SetVoiceParam, vdx::Source::Programmer, params::opParam(2, params::kOpLevel), 77);
  a.post(vdx::CmdType::SetFunctionParam, vdx::Source::Programmer, params::kFnPortaTime, 33);
  a.post(vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::Volume), 100);
  a.post(vdx::CmdType::SetSetting, vdx::Source::Programmer, int(vdx::SettingId::Engine), 1);
  a.post(vdx::CmdType::SetSetting, vdx::Source::Programmer, int(vdx::SettingId::Zoom), 120);
  a.settle();

  const double budget = double(kBlock) / kRate;
  auto playback = [](VdxProcessor& proc, std::atomic<bool>& stop, double& worst, int& over, int& blocks) {
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer m;
    const double budgetSec = double(kBlock) / kRate;
    while (!stop.load()) {
      m.clear();
      if (blocks % 40 == 0) m.addEvent(juce::MidiMessage::noteOn(1, 48 + blocks % 24, uint8_t(100)), blocks % kBlock);
      if (blocks % 40 == 20) m.addEvent(juce::MidiMessage::allNotesOff(1), 0);
      const auto t0 = std::chrono::steady_clock::now();
      proc.processBlock(buf, m);
      const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      worst = std::max(worst, dt);
      if (dt > budgetSec) ++over;
      ++blocks;
    }
  };

  // getStateInformation on this thread while the audio thread plays.
  std::atomic<bool> stop{false};
  double worstA = 0;
  int overA = 0, blocksA = 0;
  std::thread audioA([&] { playback(*a.p, stop, worstA, overA, blocksA); });
  juce::MemoryBlock blob;
  a.p->getStateInformation(blob);
  REQUIRE(blob.getSize() > 0);
  for (int i = 0; i < 300; ++i) {
    juce::MemoryBlock again;
    a.p->getStateInformation(again);
    REQUIRE(again == blob);
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  while (blocksA < 200) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  stop = true;
  audioA.join();

  // setStateInformation into a new instance, also during playback.
  Rig b;
  b.settle();
  stop = false;
  double worstB = 0;
  int overB = 0, blocksB = 0;
  std::thread audioB([&] { playback(*b.p, stop, worstB, overB, blocksB); });
  while (blocksB < 20) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  b.p->setStateInformation(blob.getData(), int(blob.getSize()));
  const int loadedAt = blocksB;
  while (blocksB < loadedAt + 100) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  stop = true;
  audioB.join();
  b.drain();

  juce::MemoryBlock blobB;
  b.p->getStateInformation(blobB);
  CHECK(blobB == blob);
  // The loaded state reached the host parameters (R-S4) and the program list.
  for (int i = 0; i < 174; ++i) CHECK(hostValue(*b.p, i) == hostValue(*a.p, i));
  CHECK(b.p->getCurrentProgram() == 4);
  CHECK(b.p->zoomSetting() == 120);

  INFO("worst block A " << worstA * 1e3 << " ms, B " << worstB * 1e3 << " ms, budget " << budget * 1e3 << " ms");
  CHECK(overA == 0);
  CHECK(overB == 0);
}

// ---- AC-S4.1 -------------------------------------------------------------------

TEST_CASE("AC-S4.1 voice selection notifies each changed host parameter once and nothing more for 2 s") {
  Rig r;
  r.settle();
  CountingListener listener;
  r.p->addListener(&listener);

  auto expectSelection = [&](int fromSlot, int toSlot) {
    const auto a = defaultVoice(fromSlot), b = defaultVoice(toSlot);
    std::set<int> changed;
    for (int i = 0; i < 145; ++i)
      if (a[size_t(i)] != b[size_t(i)]) changed.insert(vdx::plugin::hostIndexOfVoice(i));
    REQUIRE(!changed.empty());
    // 2 s of blocks with the timer running at ~60 Hz (every other 10.7 ms block).
    const int blocks = int(std::ceil(2.0 * kRate / kBlock)) + 1;
    for (int k = 0; k < blocks; ++k) {
      r.block();
      if (k % 2 == 1) r.drain();
    }
    for (int i = 0; i < 174; ++i) {
      INFO("host parameter " << i << " " << paramId(*r.p, i));
      CHECK(listener.counts[i] == (changed.count(i) ? 1 : 0));
    }
    for (int i = 0; i < 145; ++i) CHECK(hostValue(*r.p, vdx::plugin::hostIndexOfVoice(i)) == b[size_t(i)]);
    int reapplied = 0;
    for (const auto& e : r.events)
      if (isParamEvent(e) && e.src == vdx::Source::Host) ++reapplied;
    CHECK(reapplied == 0);
    listener.counts.clear();
    r.events.clear();
  };

  SECTION("MIDI program change") {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::programChange(1, 4), 0);  // INT 5 BRASS
    r.block(&m);
    expectSelection(1, 5);
  }
  SECTION("host program") {
    r.p->setCurrentProgram(3);  // INT 4 BASS
    CHECK(r.p->getCurrentProgram() == 3);
    expectSelection(1, 4);
    CHECK(r.p->getCurrentProgram() == 3);
  }
  SECTION("bank manager selection") {
    juce::ignoreUnused(r.p->handleUiCommand(R"({"t":"selectVoice","bank":0,"slot":2})"));
    expectSelection(1, 2);
  }
  r.p->removeListener(&listener);
}

// ---- AC-S1.1 (host automation path) ----------------------------------------------

TEST_CASE("AC-S1.1 host automation of every host parameter equals the core-command path, one event per change") {
  Rig host, ref;
  host.settle();
  ref.settle();
  CountingListener hostListener;
  host.p->addListener(&hostListener);

  std::mt19937 rng(1);
  const auto& table = vdx::plugin::hostParams();
  int refMask = 0x3F;
  int refAssign[4] = {1, 0, 0, 0};  // function defaults 71/73/75/77
  for (int i = 0; i < 174; ++i) {
    const auto& info = table[size_t(i)];
    const int cur = hostValue(*host.p, i);
    int v = cur;
    std::uniform_int_distribution<int> dist(0, info.max);
    while (v == cur) v = dist(rng);
    INFO("host parameter " << info.id << " " << cur << " -> " << v);

    // Host automation: the host writes the parameter; it applies at the next block.
    host.events.clear();
    auto* prm = ranged(*host.p, i);
    prm->setValue(prm->convertTo0to1(float(v)));
    host.block();
    host.drain();
    int hostEvents = 0;
    for (const auto& e : host.events)
      if (isParamEvent(e)) {
        ++hostEvents;
        CHECK(e.src == vdx::Source::Host);
      }
    CHECK(hostEvents == 1);

    // The core-command path on a second instance.
    vdx::Command c;
    c.src = vdx::Source::Programmer;
    switch (info.kind) {
      case HostKind::Voice:
        c.type = vdx::CmdType::SetVoiceParam;
        c.a = info.index;
        c.b = v;
        break;
      case HostKind::OpSwitch:
        refMask = v ? (refMask | (1 << info.bit)) : (refMask & ~(1 << info.bit));
        c.type = vdx::CmdType::SetVoiceParam;
        c.a = params::kOpMask;
        c.b = refMask;
        break;
      case HostKind::Function:
        c.type = vdx::CmdType::SetFunctionParam;
        c.a = info.index;
        c.b = v;
        break;
      case HostKind::AssignBit: {
        int& a = refAssign[(info.index - params::kFnWheelAssign) / 2];
        a = v ? (a | (1 << info.bit)) : (a & ~(1 << info.bit));
        c.type = vdx::CmdType::SetFunctionParam;
        c.a = info.index;
        c.b = a;
        break;
      }
      case HostKind::MasterTune:
        c.type = vdx::CmdType::SetGlobal;
        c.a = int(vdx::Global::MasterTune);
        c.b = v;
        break;
    }
    ref.events.clear();
    ref.p->instrument().post(c);
    ref.block();
    ref.drain();
    int refEvents = 0;
    for (const auto& e : ref.events)
      if (isParamEvent(e)) ++refEvents;
    CHECK(refEvents == 1);
  }
  host.settle();
  ref.settle();

  juce::MemoryBlock a, b;
  host.p->getStateInformation(a);
  ref.p->getStateInformation(b);
  CHECK(a == b);
  // The reference reported its core changes to its host parameters (R-S4):
  // both instances now show the same values; the host path reported nothing back.
  for (int i = 0; i < 174; ++i) CHECK(hostValue(*host.p, i) == hostValue(*ref.p, i));
  CHECK(hostListener.total == 0);
  CHECK(host.p->hostReportsMade() == 0);
  host.p->removeListener(&hostListener);
}

// ---- AC-M1.1 (plugin part) -----------------------------------------------------------

namespace {

double noteRms(Rig& r, int channel) {
  juce::MidiBuffer on, off;
  on.addEvent(juce::MidiMessage::noteOn(channel, 60, uint8_t(100)), 0);
  off.addEvent(juce::MidiMessage::noteOff(channel, 60), 0);
  double sum = 0;
  int n = 0;
  bool identical = true;
  for (int k = 0; k < 20; ++k) {
    r.block(k == 0 ? &on : nullptr);
    const float* L = r.buffer.getReadPointer(0);
    const float* R = r.buffer.getReadPointer(1);
    for (int i = 0; i < kBlock; ++i) {
      sum += double(L[i]) * L[i];
      identical = identical && L[i] == R[i];
      ++n;
    }
  }
  CHECK(identical);  // the mono engine signal on every output channel
  r.block(&off);
  for (int k = 0; k < 60; ++k) r.block();  // release tail
  r.drain();
  return std::sqrt(sum / n);
}

}  // namespace

TEST_CASE("AC-M1.1 plugin: receive channel 3 with omni off plays only channel 3; omni on plays both") {
  SECTION("omni off") {
    Rig r;
    r.post(vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::MidiChannel), 3);
    r.post(vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::Omni), 0);
    r.settle();
    CHECK(noteRms(r, 1) < 1e-6);
    CHECK(noteRms(r, 3) > 1e-3);
  }
  SECTION("omni on") {
    Rig r;
    r.post(vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::MidiChannel), 3);
    r.post(vdx::CmdType::SetGlobal, vdx::Source::Programmer, int(vdx::Global::Omni), 1);
    r.settle();
    CHECK(noteRms(r, 1) > 1e-3);
    CHECK(noteRms(r, 3) > 1e-3);
  }
}

// ---- AC-K2.2 (plugin default) ------------------------------------------------------

TEST_CASE("AC-K2.2 plugin: computer keyboard play defaults to off; a restored state overrides it") {
  auto keyboardPlay = [](Rig& r) {
    r.events.clear();
    r.post(vdx::CmdType::RequestSnapshot, vdx::Source::Internal);
    r.block();
    r.drain();
    int v = -1;
    for (const auto& e : r.events)
      if (e.type == vdx::EvType::SettingValue && e.a == int(vdx::SettingId::KeyboardPlay)) v = e.b;
    return v;
  };
  {
    VdxProcessor fresh;  // a host saving before any audio has run
    juce::MemoryBlock saved;
    fresh.getStateInformation(saved);
    Rig probe;
    probe.p->setStateInformation(saved.getData(), int(saved.getSize()));
    probe.settle();
    CHECK(keyboardPlay(probe) == 0);
  }
  Rig a;
  a.settle();
  CHECK(keyboardPlay(a) == 0);
  a.post(vdx::CmdType::SetSetting, vdx::Source::Programmer, int(vdx::SettingId::KeyboardPlay), 1);
  a.settle();
  juce::MemoryBlock blob;
  a.p->getStateInformation(blob);

  Rig b;  // a new instance with a saved state: the state wins
  b.p->setStateInformation(blob.getData(), int(blob.getSize()));
  b.settle();
  CHECK(keyboardPlay(b) == 1);

  b.p->setStateInformation(nullptr, 0);  // empty state: first-run state, plugin default
  b.settle();
  CHECK(keyboardPlay(b) == 0);
}

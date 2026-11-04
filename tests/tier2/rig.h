// Test rig for the Tier 2 core tests: drives the public vdx::Instrument API
// (post -> render -> pollEvent) and tracks the latest LCD/LED/lamps/panel.
#pragma once

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core.h"
#include "panel/core_impl.h"
#include "vdx/params.h"
#include "vdx/vdx.h"

namespace rig {

using vdx::CmdType;
using vdx::Command;
using vdx::Event;
using vdx::EvType;
using vdx::Source;

struct Ev {
  Event e;
  std::vector<uint8_t> payload;
};

inline Command cmd(CmdType t, Source s = Source::Programmer, int a = 0, int b = 0, int c = 0) {
  Command x;
  x.type = t;
  x.src = s;
  x.a = a;
  x.b = b;
  x.c = c;
  return x;
}

class Rig {
 public:
  explicit Rig(double rate = vdx::kInternalRate) : rate_(rate), inst(rate, 512) {
    post(cmd(CmdType::RequestSnapshot, Source::Internal));
    render(64);
    log.clear();
  }

  double rate_;
  vdx::Instrument inst;
  std::vector<Ev> log;
  std::string l1 = "", l2 = "";
  int curRow = 0, curCol = 0;
  bool cursor = false;
  int led = 0, ledDot = 0;
  int lampNum = 0, lampMode = 0, lampBlink = 0;
  int panelMode = 0, panelPage = 0, panelOp = 1, panelFlags = 0;
  float dePos = -1.f;
  std::vector<uint8_t> midiOutBytes;

  vdx::Core::Impl& core() { return inst.core().impl(); }

  void post(const Command& c) { REQUIRE(inst.post(c)); }
  void postPayload(const Command& c, const uint8_t* d, int n) { REQUIRE(inst.postPayload(c, d, n)); }

  // Render n host samples (in chunks) and drain all events.
  void render(int n = 64) {
    std::vector<float> L(512), R(512);
    while (n > 0) {
      const int k = n < 512 ? n : 512;
      inst.render(L.data(), R.data(), k);
      n -= k;
      drain();
    }
    uint8_t buf[65536];
    int got;
    while ((got = inst.readMidiOut(buf, sizeof buf)) > 0) midiOutBytes.insert(midiOutBytes.end(), buf, buf + got);
  }
  void ms(double milliseconds) { render(int(milliseconds * rate_ / 1000.0 + 0.5)); }

  void drain() {
    Event e;
    while (inst.pollEvent(e)) {
      Ev x{e, {}};
      if (e.slot != 0xFFFFFFFFu) {
        x.payload.resize(e.len);
        inst.readPayload(e, x.payload.data(), int(e.len));
      }
      apply(e);
      log.push_back(std::move(x));
    }
  }

  void apply(const Event& e) {
    switch (e.type) {
      case EvType::Lcd:
        l1.assign(e.text, 16);
        l2.assign(e.text + 16, 16);
        curRow = e.a;
        curCol = e.b;
        cursor = e.c != 0;
        break;
      case EvType::Led:
        led = e.a;
        ledDot = e.b;
        break;
      case EvType::Lamps:
        lampNum = e.a;
        lampMode = e.b;
        lampBlink = e.c;
        break;
      case EvType::PanelState:
        panelMode = e.a;
        panelPage = e.b;
        panelOp = e.c;
        panelFlags = e.d;
        break;
      case EvType::DataEntryPos: dePos = e.f; break;
      default: break;
    }
  }

  // ---- inputs ----
  void down(int btn) { post(cmd(CmdType::Button, Source::Panel, btn, 1)); }
  void up(int btn) { post(cmd(CmdType::Button, Source::Panel, btn, 0)); }
  void press(int btn) {
    down(btn);
    up(btn);
    render(64);
  }
  void presses(std::initializer_list<int> btns) {
    for (int b : btns) press(b);
  }
  void dataEntry(float pos) {
    Command c = cmd(CmdType::DataEntry, Source::Panel);
    c.f = pos;
    post(c);
    render(64);
  }
  void voice(Source s, int i, int v) {
    post(cmd(CmdType::SetVoiceParam, s, i, v));
    render(64);
  }
  void host(int i, int v) { voice(Source::Host, i, v); }
  void prog(int i, int v) { voice(Source::Programmer, i, v); }
  void fn(Source s, int n, int v) {
    post(cmd(CmdType::SetFunctionParam, s, n, v));
    render(64);
  }
  void hostFn(int n, int v) { fn(Source::Host, n, v); }
  void progFn(int n, int v) { fn(Source::Programmer, n, v); }
  void global(vdx::Global g, int v, Source s = Source::Programmer) {
    post(cmd(CmdType::SetGlobal, s, int(g), v));
    render(64);
  }
  void simple(CmdType t, Source s = Source::Programmer, int a = 0, int b = 0, int c = 0) {
    post(cmd(t, s, a, b, c));
    render(64);
  }
  void midi(const std::vector<uint8_t>& bytes) {
    inst.midi(bytes.data(), int(bytes.size()), 0);
    render(64);
  }
  void newCartridge() { simple(CmdType::NewCartridge, Source::BankManager); }
  void splashAndWait() {
    simple(CmdType::StartSplash, Source::Internal);
    ms(1600);
  }

  // ---- inspection ----
  const vdx::state::Persist& ps() { return core().ps; }
  int raw(int i) { return core().ps.edit.raw[i]; }
  int fnv(int n) { return core().ps.fn[n - 64]; }
  int globalv(vdx::Global g) { return core().ps.globals[int(g)]; }
  bool edited() { return core().ps.edited != 0; }
  int count(EvType t) const {
    int n = 0;
    for (const auto& x : log)
      if (x.e.type == t) ++n;
    return n;
  }
  int countParam(EvType t, int index) const {
    int n = 0;
    for (const auto& x : log)
      if (x.e.type == t && x.e.a == index) ++n;
    return n;
  }
  const Ev* last(EvType t) const {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
      if (it->e.type == t) return &*it;
    return nullptr;
  }
  // Complete SysEx/MIDI messages transmitted since the last call.
  std::vector<std::vector<uint8_t>> takeMidiOut() {
    std::vector<std::vector<uint8_t>> msgs;
    size_t i = 0;
    while (i < midiOutBytes.size()) {
      size_t j = i + 1;
      if (midiOutBytes[i] == 0xF0) {
        while (j < midiOutBytes.size() && midiOutBytes[j] != 0xF7) ++j;
        ++j;
      } else {
        while (j < midiOutBytes.size() && midiOutBytes[j] < 0x80) ++j;
      }
      msgs.emplace_back(midiOutBytes.begin() + long(i), midiOutBytes.begin() + long(j > midiOutBytes.size() ? midiOutBytes.size() : j));
      i = j;
    }
    midiOutBytes.clear();
    return msgs;
  }
};

// Pad a string to 16 characters.
inline std::string pad16(std::string s) {
  s.resize(16, ' ');
  return s;
}

inline std::vector<uint8_t> noteOn(int ch1, int note, int vel) {
  return {uint8_t(0x90 | ((ch1 - 1) & 15)), uint8_t(note), uint8_t(vel)};
}
inline std::vector<uint8_t> noteOff(int ch1, int note) { return {uint8_t(0x80 | ((ch1 - 1) & 15)), uint8_t(note), 0}; }
inline std::vector<uint8_t> cc(int ch1, int n, int v) { return {uint8_t(0xB0 | ((ch1 - 1) & 15)), uint8_t(n), uint8_t(v)}; }
inline std::vector<uint8_t> pc(int ch1, int p) { return {uint8_t(0xC0 | ((ch1 - 1) & 15)), uint8_t(p)}; }

inline std::string sourceDir() { return VDX_SOURCE_DIR; }

}  // namespace rig

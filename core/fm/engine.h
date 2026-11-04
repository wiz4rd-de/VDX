// VDX sound engine (SPEC §7.1). Single-threaded: owned and driven by vdx::Core
// on the audio thread. Renders blocks of kBlock samples at the engine rate
// (49,096 Hz, or the host rate when "render at host rate" is on).
#pragma once

#include <cstdint>

#include "vdx/vdx.h"

namespace vdx::fm {

// Function parameters 64..77 as the engine needs them (§4.2).
struct FunctionState {
  int mono = 0;        // 64
  int bendRange = 2;   // 65
  int bendStep = 0;    // 66
  int portaMode = 0;   // 67
  int gliss = 0;       // 68
  int portaTime = 0;   // 69
  int wheelRange = 99, wheelAssign = 1;  // 70 / 71
  int footRange = 0, footAssign = 0;     // 72 / 73
  int breathRange = 0, breathAssign = 0; // 74 / 75
  int atRange = 0, atAssign = 0;         // 76 / 77
};

// Test-only switches and probes (AC-E3.1, AC-E6.1, AC-E7.x). Never set in the app.
struct EngineDebug {
  bool noQuantise = false;      // turn off Mk I frequency quantisation
  bool forceNoDac = false;      // turn off the DAC stage regardless of settings
  bool noCarrierScale = false;  // (unused by default)
  // If set, every phase-increment conversion appends its logfreq here.
  int32_t* logfreqLog = nullptr;
  int logfreqCap = 0;
  int logfreqCount = 0;
};

class Engine {
 public:
  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Not real-time safe (may build tables). Stops all voices, keeps parameters.
  void prepare(double engineRate);
  double rate() const;

  // bytes 0..154 = effective voice values, [155] = operator mask (bit 5 = OP1 ... bit 0 = OP6).
  // Sounding notes switch to the new data at the next block (F-6.4).
  void setVoice(const uint8_t* voice156);
  void setFunction(const FunctionState&);
  void setMasterTune(int v0to127);
  // Takes effect at the next block without stopping notes (R-E14).
  void setMode(EngineMode mode, bool dac, bool classicVelocity);

  void noteOn(int note, int velocity);
  void noteOff(int note);
  void sustain(bool on);
  void portamentoSwitch(bool on);
  void pitchBend(int value14);  // 0..16383, 8192 = centre
  void modWheel(int v);         // 0..127
  void breath(int v);
  void foot(int v);
  void aftertouch(int v);
  void allNotesOff();  // CC123: release every voice
  void allSoundOff();  // CC120: silence every voice now

  // One block (kBlock samples). dacTap: the int32 DAC tap (R-E9) when the DAC
  // stage is active, otherwise the voice sum >> 11 (unclamped). out: engine
  // output before the output stage (R-E10).
  void render(int32_t* dacTap, float* out);

  // Peak |operator output| (Q24) per operator (index 0 = OP1) since the last call.
  void takeOperatorPeaks(int32_t peak[6]);
  int activeVoices() const;
  EngineDebug& debug();

  struct Impl;
  Impl& impl();

 private:
  Impl* impl_;
};

}  // namespace vdx::fm

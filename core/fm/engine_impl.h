// Engine internals, visible to tests (tests/tier1) and tools.
#pragma once

#include <cstdint>

#include "fm/engine.h"
#include "fm/env.h"
#include "fm/lfo_pitchenv.h"

namespace vdx::fm {

// MSFA FmAlgorithm flags (fm_core.h).
enum AlgFlags : int {
  kOutBusOne = 1 << 0,
  kOutBusTwo = 1 << 1,
  kOutBusAdd = 1 << 2,
  kInBusOne = 1 << 4,
  kInBusTwo = 1 << 5,
  kFbIn = 1 << 6,
  kFbOut = 1 << 7,
};

// Algorithm table access (MSFA FmCore::algorithms, ops in order OP6..OP1).
int algorithmFlags(int algorithm, int index);
// Carrier count derived from the table.
int carrierCount(int algorithm);

int32_t noteLogfreq(int note);  // standard tuning (MSFA dx7note.cc)
int32_t oscFreq(int note, int mode, int coarse, int fine, int detune);
int scaleVelocity(int velocity, int sensitivity);
int scaleRate(int note, int sensitivity);
int scaleLevel(int note, int breakPoint, int leftDepth, int rightDepth, int leftCurve, int rightCurve);
// R-E9: 15-bit clamp + sign/11-bit mantissa/2-bit exponent quantiser on s = voiceSum >> 11.
int32_t dacQuantise(int32_t s);
// Mk I operator kernel (R-E3) for one sample.
int32_t mk1Kernel(uint32_t phasePlusMod, int L, int carrierAtt);
extern const uint16_t kLogsin[1024];
extern const uint16_t kExptab[1024];
extern const int32_t kFreqlut49096[1025];
extern const int32_t kFinetab[100];

struct OpRt {
  Env env;
  uint32_t phase = 0;
  int32_t base = 0;   // Q24 logfreq (coarse, fine, detune included)
  bool fixed = false;
  uint32_t ams = 0;   // ampmodsenstab value
  int32_t gainOut = 0;
  int32_t lastLogfreq = 0;
};

struct VoiceRt {
  bool active = false;
  bool keyDown = false;
  bool sustained = false;
  int note = 0;      // incoming note (note-off matching)
  int playNote = 0;  // note + transpose - 24, folded into 0..127
  int velocity = 0;  // effective velocity
  uint64_t serial = 0;
  OpRt op[6];        // storage order: index 0 = OP6 ... 5 = OP1
  PitchEnv peg;
  int32_t fb[2] = {0, 0};
  int32_t porta = 0;  // Q24 offset gliding towards 0
  int32_t lastPitchMod = 0;
};

struct Engine::Impl {
  Impl();
  void prepare(double rate);
  void applyVoiceData();
  void startVoice(VoiceRt& v, int note, int velocity, int32_t portaFrom, bool retrigger);
  void computeOps(VoiceRt& v, bool init);
  void setNotePitch(VoiceRt& v, int note);
  int pickVoice(int note) const;
  int foldNote(int note) const;
  bool anyKeyHeld() const;
  bool portaActive() const;
  int32_t currentPitch(const VoiceRt& v) const;  // logfreq of the voice's note incl. glide offset
  void refreshControllers();
  void renderVoice(VoiceRt& v, int32_t* vsum);
  void noteOn(int note, int velocity);
  void noteOff(int note);
  void releaseVoice(VoiceRt& v);
  void render(int32_t* tap, float* out);

  double rate = 0.0;
  bool internalRate = true;
  int32_t freqlut[1025] = {};
  int pegUnit = 0;
  int32_t portaRate[100] = {};
  uint64_t egRatioQ32 = 1ull << 32;
  uint64_t egAcc = 0;
  uint32_t egClock = 0;
  double svfA1 = 0, svfA2 = 0, svfA3 = 0, ic1 = 0, ic2 = 0;
  uint64_t blockCount = 0;

  uint8_t voice[156] = {};
  bool voiceDirty = true;
  int alg = 0;
  int fbShift = 16;
  uint32_t pmd = 0, amd = 0;
  int pms = 0;
  int lfoKey[4] = {-1, -1, -1, -1};

  FunctionState fn;
  int masterTune = 64;
  EngineMode mode = EngineMode::MkI;
  bool dac = true;
  bool classicVelocity = false;

  int bend = 8192, modwheel = 0, breathCc = 0, footCc = 0, atCc = 0;
  bool sustainOn = false;
  bool portaSwitch = true;
  int ampMod = 0, pitchMod = 0, egMod = 127;

  Lfo lfo;
  VoiceRt voices[kVoices];
  uint64_t serialCounter = 0;
  int lastPlayNote = -1;
  int monoStack[128] = {};
  int monoCount = 0;

  int32_t peaks[6] = {};
  int32_t blkLfoVal = 0, blkLfoDelay = 0, blkPitchBase = 0;
  int32_t blkAmp = 0;
  EngineDebug dbg;

  // scratch
  int32_t L[6][kBlock] = {};
  uint8_t clocks[kBlock] = {};
  uint32_t clockStart[kBlock] = {};
};

}  // namespace vdx::fm

// Parameter numbering and ranges (SPEC §4.1–§4.4, §4.9). Header-only.
#pragma once

#include <cstdint>

namespace vdx::params {

// ---- Voice parameters (§4.1) -------------------------------------------
// Operator block offsets (21 bytes per operator, OP6 first in storage).
enum OpOffset : int {
  kOpR1 = 0, kOpR2 = 1, kOpR3 = 2, kOpR4 = 3,
  kOpL1 = 4, kOpL2 = 5, kOpL3 = 6, kOpL4 = 7,
  kOpBreakPoint = 8, kOpLeftDepth = 9, kOpRightDepth = 10,
  kOpLeftCurve = 11, kOpRightCurve = 12, kOpRateScaling = 13,
  kOpAms = 14, kOpKvs = 15, kOpLevel = 16, kOpMode = 17,
  kOpCoarse = 18, kOpFine = 19, kOpDetune = 20,
  kOpBytes = 21
};

// op = 1..6 -> first byte of OPop's block.
constexpr int opBase(int op) { return (6 - op) * kOpBytes; }
constexpr int opParam(int op, int offset) { return opBase(op) + offset; }
// voice index 0..125 -> operator 1..6 (0 if global).
constexpr int opOfIndex(int index) { return index < 126 ? 6 - index / kOpBytes : 0; }
constexpr int offsetOfIndex(int index) { return index % kOpBytes; }

enum GlobalIndex : int {
  kPegR1 = 126, kPegR2 = 127, kPegR3 = 128, kPegR4 = 129,
  kPegL1 = 130, kPegL2 = 131, kPegL3 = 132, kPegL4 = 133,
  kAlgorithm = 134, kFeedback = 135, kOscKeySync = 136,
  kLfoSpeed = 137, kLfoDelay = 138, kLfoPmd = 139, kLfoAmd = 140,
  kLfoKeySync = 141, kLfoWave = 142, kPitchModSens = 143, kTranspose = 144,
  kName = 145,  // 145..154
  kOpMask = 155,
  kVoiceBytes = 155,
  kVoiceParams = 156
};

constexpr int kOpMax[kOpBytes] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};

// Maximum effective value of a voice parameter (minimum is always 0).
constexpr int voiceMax(int index) {
  if (index < 0) return 0;
  if (index < 126) return kOpMax[index % kOpBytes];
  if (index <= 133) return 99;
  switch (index) {
    case kAlgorithm: return 31;
    case kFeedback: return 7;
    case kOscKeySync: return 1;
    case kLfoSpeed: case kLfoDelay: case kLfoPmd: case kLfoAmd: return 99;
    case kLfoKeySync: return 1;
    case kLfoWave: return 5;
    case kPitchModSens: return 7;
    case kTranspose: return 48;
    case kOpMask: return 63;
    default: break;
  }
  if (index >= kName && index < kName + 10) return 127;  // raw name byte
  return 0;
}

// Effective value (§4.1): raw clamped to range. Name bytes outside 0x20..0x7E
// display as a space but are stored as-is.
constexpr int effective(int index, int raw) {
  const int mx = voiceMax(index);
  return raw < 0 ? 0 : (raw > mx ? mx : raw);
}

// ---- Function parameters (§4.2) ----------------------------------------
enum FunctionNumber : int {
  kFnMono = 64, kFnBendRange = 65, kFnBendStep = 66, kFnPortaMode = 67,
  kFnGlissando = 68, kFnPortaTime = 69,
  kFnWheelRange = 70, kFnWheelAssign = 71,
  kFnFootRange = 72, kFnFootAssign = 73,
  kFnBreathRange = 74, kFnBreathAssign = 75,
  kFnAtRange = 76, kFnAtAssign = 77,
  kFnFirst = 64, kFnLast = 77, kFnCount = 14
};

constexpr int functionMax(int n) {
  switch (n) {
    case kFnMono: return 1;
    case kFnBendRange: return 12;
    case kFnBendStep: return 12;
    case kFnPortaMode: return 1;
    case kFnGlissando: return 1;
    case kFnPortaTime: return 99;
    case kFnWheelRange: case kFnFootRange: case kFnBreathRange: case kFnAtRange: return 99;
    case kFnWheelAssign: case kFnFootAssign: case kFnBreathAssign: case kFnAtAssign: return 7;
    default: return 0;
  }
}

constexpr int functionDefault(int n) {
  switch (n) {
    case kFnBendRange: return 2;
    case kFnWheelRange: return 99;
    case kFnWheelAssign: return 1;
    default: return 0;
  }
}

// ---- Panel globals (§4.3) ----------------------------------------------
constexpr int kMasterTuneDefault = 64;
constexpr int kMidiChannelDefault = 1;
constexpr int kVolumeDefault = 127;

// ---- Host parameters (§4.9) ---------------------------------------------
// Identifier and display-name stems for operator offsets 0..20.
constexpr const char* kOpParamId[kOpBytes] = {
  "eg_rate1", "eg_rate2", "eg_rate3", "eg_rate4",
  "eg_level1", "eg_level2", "eg_level3", "eg_level4",
  "break_point", "left_depth", "right_depth", "left_curve", "right_curve",
  "rate_scaling", "am_sens", "velocity_sens", "output_level", "osc_mode",
  "coarse", "fine", "detune"};
constexpr const char* kOpParamName[kOpBytes] = {
  "EG Rate 1", "EG Rate 2", "EG Rate 3", "EG Rate 4",
  "EG Level 1", "EG Level 2", "EG Level 3", "EG Level 4",
  "Break Point", "Left Depth", "Right Depth", "Left Curve", "Right Curve",
  "Rate Scaling", "AM Sensitivity", "Velocity Sensitivity", "Output Level", "Oscillator Mode",
  "Coarse", "Fine", "Detune"};
// Globals 126..144.
constexpr const char* kGlobalParamId[19] = {
  "g_peg_rate1", "g_peg_rate2", "g_peg_rate3", "g_peg_rate4",
  "g_peg_level1", "g_peg_level2", "g_peg_level3", "g_peg_level4",
  "g_algorithm", "g_feedback", "g_osc_key_sync", "g_lfo_speed", "g_lfo_delay",
  "g_lfo_pm_depth", "g_lfo_am_depth", "g_lfo_key_sync", "g_lfo_wave", "g_pitch_mod_sens", "g_transpose"};
constexpr const char* kGlobalParamName[19] = {
  "Pitch EG Rate 1", "Pitch EG Rate 2", "Pitch EG Rate 3", "Pitch EG Rate 4",
  "Pitch EG Level 1", "Pitch EG Level 2", "Pitch EG Level 3", "Pitch EG Level 4",
  "Algorithm", "Feedback", "Oscillator Key Sync", "LFO Speed", "LFO Delay",
  "LFO PM Depth", "LFO AM Depth", "LFO Key Sync", "LFO Wave", "Pitch Mod Sensitivity", "Transpose"};
// Controller sources for function 70..77.
constexpr const char* kCtlSourceId[4] = {"wheel", "foot", "breath", "after"};
constexpr const char* kCtlSourceName[4] = {"Wheel", "Foot", "Breath", "Aftertouch"};

}  // namespace vdx::params

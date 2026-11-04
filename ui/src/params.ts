// Parameter numbering and ranges — mirrors core/include/vdx/params.h
// (SPEC §4.1–§4.4, §4.9). Display strings always come from the core (`s` of
// param/func/global events); this file only knows indices, ranges, names and
// the unit in which a typed number is entered (R-U1).

// ---- Voice parameters (§4.1) -------------------------------------------
export const OP = {
  R1: 0, R2: 1, R3: 2, R4: 3,
  L1: 4, L2: 5, L3: 6, L4: 7,
  BREAK_POINT: 8, LEFT_DEPTH: 9, RIGHT_DEPTH: 10,
  LEFT_CURVE: 11, RIGHT_CURVE: 12, RATE_SCALING: 13,
  AMS: 14, KVS: 15, LEVEL: 16, MODE: 17,
  COARSE: 18, FINE: 19, DETUNE: 20,
} as const;
export const OP_BYTES = 21;

/** op = 1..6 → first byte of OPop's block (OP6 is stored first). */
export const opBase = (op: number): number => (6 - op) * OP_BYTES;
export const opParam = (op: number, offset: number): number => opBase(op) + offset;
/** voice index 0..125 → operator 1..6 (0 for globals). */
export const opOfIndex = (index: number): number => (index < 126 ? 6 - Math.floor(index / OP_BYTES) : 0);
export const offsetOfIndex = (index: number): number => index % OP_BYTES;

export const G = {
  PEG_R1: 126, PEG_R2: 127, PEG_R3: 128, PEG_R4: 129,
  PEG_L1: 130, PEG_L2: 131, PEG_L3: 132, PEG_L4: 133,
  ALGORITHM: 134, FEEDBACK: 135, OSC_KEY_SYNC: 136,
  LFO_SPEED: 137, LFO_DELAY: 138, LFO_PMD: 139, LFO_AMD: 140,
  LFO_KEY_SYNC: 141, LFO_WAVE: 142, PITCH_MOD_SENS: 143, TRANSPOSE: 144,
  NAME: 145, // 145..154
  OP_MASK: 155,
} as const;
export const VOICE_BYTES = 155;
export const VOICE_PARAMS = 156;

const OP_MAX = [99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14];

/** Maximum effective value of a voice parameter (minimum is always 0). */
export function voiceMax(index: number): number {
  if (index < 0) return 0;
  if (index < 126) return OP_MAX[index % OP_BYTES];
  if (index <= 133) return 99;
  switch (index) {
    case G.ALGORITHM: return 31;
    case G.FEEDBACK: return 7;
    case G.OSC_KEY_SYNC: return 1;
    case G.LFO_SPEED: case G.LFO_DELAY: case G.LFO_PMD: case G.LFO_AMD: return 99;
    case G.LFO_KEY_SYNC: return 1;
    case G.LFO_WAVE: return 5;
    case G.PITCH_MOD_SENS: return 7;
    case G.TRANSPOSE: return 48;
    case G.OP_MASK: return 63;
  }
  if (index >= G.NAME && index < G.NAME + 10) return 127;
  return 0;
}

// ---- Function parameters (§4.2) ----------------------------------------
export const FN = {
  MONO: 64, BEND_RANGE: 65, BEND_STEP: 66, PORTA_MODE: 67,
  GLISSANDO: 68, PORTA_TIME: 69,
  WHEEL_RANGE: 70, WHEEL_ASSIGN: 71,
  FOOT_RANGE: 72, FOOT_ASSIGN: 73,
  BREATH_RANGE: 74, BREATH_ASSIGN: 75,
  AT_RANGE: 76, AT_ASSIGN: 77,
} as const;
export const FN_FIRST = 64;
export const FN_LAST = 77;

export function functionMax(n: number): number {
  switch (n) {
    case FN.MONO: return 1;
    case FN.BEND_RANGE: return 12;
    case FN.BEND_STEP: return 12;
    case FN.PORTA_MODE: return 1;
    case FN.GLISSANDO: return 1;
    case FN.PORTA_TIME: return 99;
    case FN.WHEEL_RANGE: case FN.FOOT_RANGE: case FN.BREATH_RANGE: case FN.AT_RANGE: return 99;
    case FN.WHEEL_ASSIGN: case FN.FOOT_ASSIGN: case FN.BREATH_ASSIGN: case FN.AT_ASSIGN: return 7;
  }
  return 0;
}

export function functionDefault(n: number): number {
  switch (n) {
    case FN.BEND_RANGE: return 2;
    case FN.WHEEL_RANGE: return 99;
    case FN.WHEEL_ASSIGN: return 1;
  }
  return 0;
}

// ---- Panel globals (§4.3) ----------------------------------------------
export const GLOBAL_RANGE = {
  masterTune: { min: 0, max: 127, def: 64 },
  midiChannel: { min: 1, max: 16, def: 1 },
  omni: { min: 0, max: 1, def: 1 },
  sysInfo: { min: 0, max: 1, def: 0 },
  protectInt: { min: 0, max: 1, def: 1 },
  protectCrt: { min: 0, max: 1, def: 1 },
  volume: { min: 0, max: 127, def: 127 },
} as const;

// ---- Names (host display names, §4.9) -----------------------------------
const OP_PARAM_NAME = [
  'EG Rate 1', 'EG Rate 2', 'EG Rate 3', 'EG Rate 4',
  'EG Level 1', 'EG Level 2', 'EG Level 3', 'EG Level 4',
  'Break Point', 'Left Depth', 'Right Depth', 'Left Curve', 'Right Curve',
  'Rate Scaling', 'AM Sensitivity', 'Velocity Sensitivity', 'Output Level', 'Oscillator Mode',
  'Coarse', 'Fine', 'Detune',
];
const GLOBAL_PARAM_NAME = [
  'Pitch EG Rate 1', 'Pitch EG Rate 2', 'Pitch EG Rate 3', 'Pitch EG Rate 4',
  'Pitch EG Level 1', 'Pitch EG Level 2', 'Pitch EG Level 3', 'Pitch EG Level 4',
  'Algorithm', 'Feedback', 'Oscillator Key Sync', 'LFO Speed', 'LFO Delay',
  'LFO PM Depth', 'LFO AM Depth', 'LFO Key Sync', 'LFO Wave', 'Pitch Mod Sensitivity', 'Transpose',
];
const FUNCTION_NAME: Record<number, string> = {
  64: 'Poly/Mono', 65: 'Pitch Bend Range', 66: 'Pitch Bend Step', 67: 'Portamento Mode',
  68: 'Glissando', 69: 'Portamento Time',
  70: 'Wheel Range', 71: 'Wheel Assign', 72: 'Foot Range', 73: 'Foot Assign',
  74: 'Breath Range', 75: 'Breath Assign', 76: 'Aftertouch Range', 77: 'Aftertouch Assign',
};
export const CTL_SOURCES = ['Wheel', 'Foot', 'Breath', 'Aftertouch'] as const;
export const ASSIGN_BITS = ['Pitch', 'Amp', 'EG Bias'] as const;

export function voiceParamName(index: number): string {
  if (index < 126) return `OP${opOfIndex(index)} ${OP_PARAM_NAME[offsetOfIndex(index)]}`;
  if (index <= G.TRANSPOSE) return GLOBAL_PARAM_NAME[index - 126];
  if (index >= G.NAME && index < G.NAME + 10) return 'Voice Name';
  if (index === G.OP_MASK) return 'Operator On/Off';
  return '';
}
export const functionName = (n: number): string => FUNCTION_NAME[n] ?? '';

// ---- Range descriptions (tooltips, R-U1) --------------------------------
const NOTE_NAMES = ['C ', 'C#', 'D ', 'D#', 'E ', 'F ', 'F#', 'G ', 'G#', 'A ', 'A#', 'B '];
/** §5.3 note name: 2-character name + octave ⌊midi/12⌋ − 2. Used for range text only. */
export const noteName = (midi: number): string =>
  `${NOTE_NAMES[((midi % 12) + 12) % 12]}${Math.floor(midi / 12) - 2}`;

export function voiceRangeText(index: number): string {
  if (index < 126) {
    switch (offsetOfIndex(index)) {
      case OP.BREAK_POINT: return `${noteName(21)} – ${noteName(120)}`;
      case OP.LEFT_CURVE: case OP.RIGHT_CURVE: return '-LIN, -EXP, +EXP, +LIN';
      case OP.MODE: return 'RATIO, FIXED';
      case OP.DETUNE: return '-7 – +7';
    }
    return `0 – ${voiceMax(index)}`;
  }
  switch (index) {
    case G.ALGORITHM: return '1 – 32';
    case G.OSC_KEY_SYNC: case G.LFO_KEY_SYNC: return 'OFF, ON';
    case G.LFO_WAVE: return 'TRI, SAW DN, SAW UP, SQU, SIN, S/H';
    case G.TRANSPOSE: return `${noteName(36)} – ${noteName(84)}`;
    case G.OP_MASK: return 'OFF, ON';
  }
  return `0 – ${voiceMax(index)}`;
}

export function functionRangeText(n: number): string {
  switch (n) {
    case FN.MONO: return 'POLY, MONO';
    case FN.PORTA_MODE: return 'RETAIN/FOLLOW (poly), FINGERED/FULL TIME (mono)';
    case FN.GLISSANDO: return 'OFF, ON';
  }
  return `0 – ${functionMax(n)}`;
}

/**
 * Offset between a parameter's raw value and the number a user types (R-U1):
 * the algorithm is typed 1–32, detune −7…+7; every other parameter is typed as
 * its raw value (DECISIONS.md).
 */
export function voiceEntryOffset(index: number): number {
  if (index === G.ALGORITHM) return 1;
  if (index < 126 && offsetOfIndex(index) === OP.DETUNE) return -7;
  return 0;
}

/** Operator mask bit for OPop (bit 5 = OP1 … bit 0 = OP6). */
export const maskBit = (op: number): number => 1 << (6 - op);

/** R-P10 operator values with output level 0 (R-U2 Init operator), offsets 0..20. */
export const INIT_OPERATOR: readonly number[] = [
  99, 99, 99, 99, // rates
  99, 99, 99, 0, // levels
  0, 0, 0, // break point, depths
  0, 0, // curves -LIN
  0, 0, 0, // rate scaling, AMS, KVS
  0, // output level
  0, 1, 0, 7, // ratio mode, coarse 1, fine 0, detune 7
];

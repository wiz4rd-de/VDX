// Front-panel legends (R-P1) and the naming character map (F-4).
// Lines are pre-wrapped to fit the 29 px button pitch (§14 latitude).

/** EDIT legends (violet), above each button. Buttons 1–6 share one spanning legend. */
export const EDIT_LEGEND: readonly (readonly string[])[] = [
  [], [], [], [], [], [],
  ['ALGO-', 'RITHM', 'SELECT'], // 7
  ['FEED-', 'BACK'], // 8
  ['LFO', 'WAVE'], // 9
  ['LFO', 'SPEED'], // 10
  ['LFO', 'DELAY'], // 11
  ['LFO PM', 'DEPTH'], // 12
  ['LFO AM', 'DEPTH'], // 13
  ['LFO KEY', 'SYNC'], // 14
  ['P MOD', 'SENS'], // 15
  ['A MOD', 'SENS'], // 16
  ['OSC MODE', 'OSC KEY', 'SYNC'], // 17
  ['FREQ', 'COARSE'], // 18
  ['FREQ', 'FINE'], // 19
  ['OSC', 'DETUNE'], // 20
  ['EG', 'RATE', '1–4'], // 21
  ['EG', 'LEVEL', '1–4'], // 22
  ['BREAK', 'POINT'], // 23
  ['KEY SCL', 'CURVE', 'L/R'], // 24
  ['KEY SCL', 'DEPTH', 'L/R'], // 25
  ['RATE', 'SCALING'], // 26
  ['OUTPUT', 'LEVEL'], // 27
  ['KEY', 'VELOCITY'], // 28
  ['P EG', 'RATE', '1–4'], // 29
  ['P EG', 'LEVEL', '1–4'], // 30
  ['KEY', 'TRANS-', 'POSE'], // 31
  ['VOICE', 'NAME'], // 32
];

/** Spanning EDIT legend over buttons 1–6. */
export const OPERATOR_LEGEND = 'OPERATOR ON/OFF';

/** FUNCTION legends (brown), below each button. */
export const FUNCTION_LEGEND: readonly (readonly string[])[] = [
  ['MASTER', 'TUNE ADJ'], // 1
  ['POLY/', 'MONO'], // 2
  ['P BEND', 'RANGE'], // 3
  ['P BEND', 'STEP'], // 4
  ['PORTA', 'MODE'], // 5
  ['GLISS-', 'ANDO'], // 6
  ['PORTA', 'TIME'], // 7
  ['MIDI CH', 'SYS INFO', 'TRANSMIT'], // 8
  ['EDIT', 'RECALL'], // 9
  ['VOICE', 'INIT'], // 10
  ['CART', 'FORMAT'], // 11
  [], // 12 (no function)
  [], // 13 (no function)
  ['BATTERY', 'CHECK'], // 14
  ['CART', 'SAVE'], // 15
  ['CART', 'LOAD'], // 16
  ['WHEEL', 'RANGE'], // 17
  ['WHEEL', 'PITCH'], // 18
  ['WHEEL', 'AMP'], // 19
  ['WHEEL', 'EG BIAS'], // 20
  ['FOOT', 'RANGE'], // 21
  ['FOOT', 'PITCH'], // 22
  ['FOOT', 'AMP'], // 23
  ['FOOT', 'EG BIAS'], // 24
  ['BREATH', 'RANGE'], // 25
  ['BREATH', 'PITCH'], // 26
  ['BREATH', 'AMP'], // 27
  ['BREATH', 'EG BIAS'], // 28
  ['AFTER', 'RANGE'], // 29
  ['AFTER', 'PITCH'], // 30
  ['AFTER', 'AMP'], // 31
  ['AFTER', 'EG BIAS'], // 32
];

/** Full R-P1 names, used for the ARIA names of the numbered buttons. */
export const EDIT_NAME: readonly string[] = [
  'OPERATOR 1 ON/OFF', 'OPERATOR 2 ON/OFF', 'OPERATOR 3 ON/OFF',
  'OPERATOR 4 ON/OFF', 'OPERATOR 5 ON/OFF', 'OPERATOR 6 ON/OFF',
  'ALGORITHM SELECT', 'FEEDBACK', 'LFO WAVE', 'LFO SPEED', 'LFO DELAY', 'LFO PM DEPTH',
  'LFO AM DEPTH', 'LFO KEY SYNC', 'P MOD SENS', 'A MOD SENS', 'OSC MODE / OSC KEY SYNC',
  'FREQ COARSE', 'FREQ FINE', 'OSC DETUNE', 'EG RATE 1–4', 'EG LEVEL 1–4', 'BREAK POINT',
  'KEY SCALE CURVE L/R', 'KEY SCALE DEPTH L/R', 'RATE SCALING', 'OUTPUT LEVEL', 'KEY VELOCITY',
  'P EG RATE 1–4', 'P EG LEVEL 1–4', 'KEY TRANSPOSE', 'VOICE NAME',
];
export const FUNCTION_NAME: readonly string[] = [
  'MASTER TUNE ADJ', 'POLY/MONO', 'P BEND RANGE', 'P BEND STEP', 'PORTAMENTO MODE', 'GLISSANDO',
  'PORTAMENTO TIME', 'MIDI CH / SYS INFO / TRANSMIT', 'EDIT RECALL', 'VOICE INIT',
  'CARTRIDGE FORMAT', '', '', 'BATTERY CHECK', 'CARTRIDGE SAVE', 'CARTRIDGE LOAD',
  'WHEEL RANGE', 'WHEEL PITCH', 'WHEEL AMP', 'WHEEL EG BIAS',
  'FOOT RANGE', 'FOOT PITCH', 'FOOT AMP', 'FOOT EG BIAS',
  'BREATH RANGE', 'BREATH PITCH', 'BREATH AMP', 'BREATH EG BIAS',
  'AFTER RANGE', 'AFTER PITCH', 'AFTER AMP', 'AFTER EG BIAS',
];

/** F-4 naming characters typed by buttons 1–32. */
export const NAME_CHARS = '1234567890ABCDEFGHIJKLMNOPQRSTUV';

/** ARIA name of numbered button n (1..32): number, EDIT legend, FUNCTION legend. */
export function numberedAria(n: number): string {
  const f = FUNCTION_NAME[n - 1];
  return `${n} ${EDIT_NAME[n - 1]}${f ? ` / ${f}` : ''}`;
}

/** Mode buttons (§5.2): id, legend lines, ARIA name, column, row. */
export interface ModeButton {
  id: number;
  lines: readonly string[];
  name: string;
  col: 0 | 1;
  row: 0 | 1 | 2 | 3;
  salmon?: boolean;
}

export const MODE_BUTTONS: readonly ModeButton[] = [
  { id: 33, lines: ['MEMORY SELECT', 'INTERNAL'], name: 'MEMORY SELECT INTERNAL', col: 0, row: 0 },
  { id: 34, lines: ['MEMORY SELECT', 'CARTRIDGE'], name: 'MEMORY SELECT CARTRIDGE', col: 0, row: 1 },
  { id: 35, lines: ['EDIT', 'COMPARE'], name: 'EDIT/COMPARE', col: 0, row: 2 },
  { id: 36, lines: ['FUNCTION'], name: 'FUNCTION', col: 0, row: 3 },
  { id: 37, lines: ['STORE'], name: 'STORE', col: 1, row: 0, salmon: true },
  { id: 38, lines: ['MEMORY PROTECT', 'INTERNAL'], name: 'MEMORY PROTECT INTERNAL', col: 1, row: 1 },
  { id: 39, lines: ['MEMORY PROTECT', 'CARTRIDGE'], name: 'MEMORY PROTECT CARTRIDGE', col: 1, row: 2 },
  { id: 40, lines: ['OPERATOR', 'SELECT'], name: 'OPERATOR SELECT', col: 1, row: 3 },
];

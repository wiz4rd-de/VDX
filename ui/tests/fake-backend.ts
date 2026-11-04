// Test double for the core (development and tests only — never imported by
// production code). It records every command, echoes parameter changes with
// display strings, and can emit a plausible snapshot for visual checks.
import type { Backend } from '../src/bridge';
import type { BuildKind, Command, GlobalId, SettingId, VdxEvent } from '../src/protocol';
import { G, OP, opParam, OP_BYTES, offsetOfIndex, voiceMax, functionMax } from '../src/params';

const NOTES = ['C ', 'C#', 'D ', 'D#', 'E ', 'F ', 'F#', 'G ', 'G#', 'A ', 'A#', 'B '];
const note = (m: number): string => `${NOTES[m % 12]}${Math.floor(m / 12) - 2}`;
const CURVES = ['-LIN', '-EXP', '+EXP', '+LIN'];
const WAVES = ['TRIANGL', 'SAW DWN', 'SAW UP', 'SQUARE', 'SINE', 'S/HOLD'];

/** Display strings in the §5.3 formats (a stand-in for the core's strings). */
export function fakeParamString(i: number, v: number): string {
  if (i < 126) {
    switch (offsetOfIndex(i)) {
      case OP.BREAK_POINT:
        return note(21 + v);
      case OP.LEFT_CURVE:
      case OP.RIGHT_CURVE:
        return CURVES[v] ?? '';
      case OP.MODE:
        return v ? 'FIXED' : 'RATIO';
      case OP.DETUNE:
        return v === 7 ? '0' : v > 7 ? `+${v - 7}` : `${v - 7}`;
    }
    return String(v);
  }
  switch (i) {
    case G.ALGORITHM:
      return String(v + 1);
    case G.OSC_KEY_SYNC:
    case G.LFO_KEY_SYNC:
      return v ? 'ON' : 'OFF';
    case G.LFO_WAVE:
      return WAVES[v] ?? '';
    case G.TRANSPOSE:
      return note(36 + v);
  }
  if (i >= G.NAME && i < G.NAME + 10) return v >= 0x20 && v <= 0x7e ? String.fromCharCode(v) : ' ';
  return String(v);
}

export function fakeFuncString(i: number, v: number, mono: number): string {
  switch (i) {
    case 64:
      return v ? 'MONO' : 'POLY';
    case 67:
      return mono ? (v ? 'FULL TIME' : 'FINGERED') : v ? 'FOLLOW' : 'RETAIN';
    case 68:
      return v ? 'ON' : 'OFF';
  }
  return String(v);
}

export function fakeGlobalString(id: GlobalId, v: number): string {
  if (id === 'masterTune') {
    const c = ((v - 64) * 75) / 64;
    return `${c > 0 ? '+' : ''}${c.toFixed(1)} cents`;
  }
  if (id === 'omni' || id === 'sysInfo' || id === 'protectInt' || id === 'protectCrt') return v ? 'ON' : 'OFF';
  return String(v);
}

function opFreqString(mode: number, coarse: number, fine: number): string {
  if (!mode) {
    const r = coarse === 0 ? 50 + Math.floor(fine / 2) : coarse * (100 + fine);
    return (r / 100).toFixed(2).padStart(5, ' ');
  }
  const f = Math.round(1000 * Math.pow(10, fine / 100));
  const digits = String(f);
  const p = (coarse & 3) + 1;
  return p === 4 ? `${digits} Hz` : `${digits.slice(0, p)}.${digits.slice(p)}Hz`;
}

export type Preset = 'play' | 'edit21' | 'func17' | 'edited' | 'blank';

export interface FakeOptions {
  preset?: Preset;
  build?: BuildKind;
  midi?: 'ok' | 'unavailable' | 'nosysex';
  idb?: boolean;
  audio?: boolean;
  cartridge?: boolean;
  /** Emit events synchronously (tests) instead of on a microtask. */
  sync?: boolean;
}

export class FakeBackend implements Backend {
  readonly kind = 'test' as const;
  readonly sent: Command[] = [];
  params: number[] = new Array(156).fill(0);
  func: number[] = new Array(78).fill(0);
  globals: Record<GlobalId, number> = {
    masterTune: 64, midiChannel: 1, omni: 1, sysInfo: 0, protectInt: 1, protectCrt: 0, volume: 110,
  };
  settings: Record<SettingId, number> = {
    engine: 0, dac: 1, hostRate: 0, classicVelocity: 0, keyboardPlay: 1, zoom: 0, opOrder: 0,
  };
  private cb: ((events: VdxEvent[]) => void) | null = null;
  private readonly opts: FakeOptions;

  constructor(opts: FakeOptions = {}) {
    this.opts = opts;
    this.loadDemoVoice();
  }

  onEvents(cb: (events: VdxEvent[]) => void): void {
    this.cb = cb;
  }

  emit(events: VdxEvent[]): void {
    if (!this.cb) return;
    const cb = this.cb;
    if (this.opts.sync) cb(events);
    else queueMicrotask(() => cb(events));
  }

  send(cmd: Command): void {
    this.sent.push(cmd);
    switch (cmd.t) {
      case 'snapshot':
        if (this.opts.preset !== 'blank') this.emit(this.snapshot());
        break;
      case 'setParam': {
        const v = Math.min(voiceMax(cmd.i), Math.max(0, cmd.v));
        this.params[cmd.i] = v;
        const ev: VdxEvent[] = [{ e: 'param', i: cmd.i, v, s: fakeParamString(cmd.i, v), src: 'programmer' }];
        if (cmd.i < 126) ev.push(this.opFreq(6 - Math.floor(cmd.i / OP_BYTES)));
        this.emit(ev);
        break;
      }
      case 'setFunc': {
        const v = Math.min(functionMax(cmd.i), Math.max(0, cmd.v));
        this.func[cmd.i] = v;
        this.emit([{ e: 'func', i: cmd.i, v, s: fakeFuncString(cmd.i, v, this.func[64]), src: 'programmer' }]);
        break;
      }
      case 'setGlobal':
        this.globals[cmd.id] = cmd.v;
        this.emit([{ e: 'global', id: cmd.id, v: cmd.v, s: fakeGlobalString(cmd.id, cmd.v) }]);
        break;
      case 'setSetting':
        this.settings[cmd.id] = cmd.v;
        this.emit([{ e: 'setting', id: cmd.id, v: cmd.v }]);
        break;
    }
  }

  /** Commands of one type (test helper). */
  of<T extends Command['t']>(t: T): Extract<Command, { t: T }>[] {
    return this.sent.filter((c): c is Extract<Command, { t: T }> => c.t === t);
  }

  private opFreq(op: number): VdxEvent {
    return {
      e: 'opFreq',
      op,
      s: opFreqString(
        this.params[opParam(op, OP.MODE)],
        this.params[opParam(op, OP.COARSE)],
        this.params[opParam(op, OP.FINE)],
      ),
    };
  }

  private loadDemoVoice(): void {
    // Invented demo values (not a factory voice).
    const ops: Record<number, number[]> = {
      1: [96, 25, 25, 67, 99, 75, 0, 0, 39, 20, 40, 0, 3, 3, 0, 2, 99, 0, 1, 0, 7],
      2: [95, 50, 35, 78, 99, 75, 0, 0, 0, 0, 0, 0, 0, 3, 0, 7, 62, 0, 14, 0, 7],
      3: [95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 96, 0, 1, 0, 8],
      4: [95, 29, 20, 50, 99, 95, 0, 0, 48, 0, 54, 0, 1, 3, 0, 6, 82, 0, 1, 0, 6],
      5: [95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 98, 1, 2, 0, 7],
      6: [95, 29, 20, 50, 99, 95, 0, 0, 41, 60, 20, 1, 2, 3, 1, 6, 70, 0, 1, 50, 9],
    };
    for (let op = 1; op <= 6; op++) for (let k = 0; k < OP_BYTES; k++) this.params[opParam(op, k)] = ops[op][k];
    [94, 67, 95, 60, 50, 50, 50, 50].forEach((v, k) => (this.params[G.PEG_R1 + k] = v));
    this.params[G.ALGORITHM] = 4;
    this.params[G.FEEDBACK] = 6;
    this.params[G.OSC_KEY_SYNC] = 1;
    this.params[G.LFO_SPEED] = 34;
    this.params[G.LFO_DELAY] = 33;
    this.params[G.LFO_PMD] = 0;
    this.params[G.LFO_AMD] = 0;
    this.params[G.LFO_KEY_SYNC] = 0;
    this.params[G.LFO_WAVE] = 4;
    this.params[G.PITCH_MOD_SENS] = 3;
    this.params[G.TRANSPOSE] = 24;
    Array.from('E.PIANO   ').forEach((c, k) => (this.params[G.NAME + k] = c.charCodeAt(0)));
    this.params[G.OP_MASK] = 0x3f;
    this.func[64] = 0;
    this.func[65] = 2;
    this.func[69] = 0;
    this.func[70] = 99;
    this.func[71] = 1;
    this.func[72] = 0;
    this.func[74] = 50;
    this.func[75] = 2;
  }

  snapshot(): VdxEvent[] {
    const ev: VdxEvent[] = [];
    const o = this.opts;
    ev.push({
      e: 'env',
      build: o.build ?? 'web',
      midi: o.midi ?? 'ok',
      inputs: [
        { id: 'in1', name: 'USB MIDI Interface Port 1', on: true },
        { id: 'in2', name: 'An Unusually Long Keyboard Controller Name With Several Words MIDI 2', on: false },
      ],
      outputs: [{ id: 'out1', name: 'USB MIDI Interface Port 1' }],
      output: 'out1',
      idb: o.idb ?? true,
      audio: o.audio ?? true,
    });
    for (let i = 0; i < 156; i++) ev.push({ e: 'param', i, v: this.params[i], s: fakeParamString(i, this.params[i]), src: 'state' });
    for (let i = 64; i <= 77; i++)
      ev.push({ e: 'func', i, v: this.func[i], s: fakeFuncString(i, this.func[i], this.func[64]), src: 'state' });
    for (const id of Object.keys(this.globals) as GlobalId[])
      ev.push({ e: 'global', id, v: this.globals[id], s: fakeGlobalString(id, this.globals[id]) });
    for (const id of Object.keys(this.settings) as SettingId[]) ev.push({ e: 'setting', id, v: this.settings[id] });
    const internal = ['INIT VOICE', 'E.PIANO', 'BELL', 'BASS', 'BRASS', 'ORGAN', 'MARIMBA', 'STRINGS', 'PAD'];
    const crt = ['GLASS KEYS', 'WOOD BASS', 'CHIME', 'SOFT PAD', 'BRIGHT BRS', 'TINE EP', 'FLUTE', 'CLAV'];
    for (let s = 1; s <= 32; s++) {
      ev.push({ e: 'voiceName', bank: 0, slot: s, name: (internal[s - 1] ?? 'INIT VOICE').padEnd(10) });
      ev.push({ e: 'voiceName', bank: 1, slot: s, name: (crt[(s - 1) % crt.length] ?? 'INIT VOICE').padEnd(10) });
    }
    const crtOn = o.cartridge ?? true;
    ev.push({ e: 'cartridge', present: crtOn, changed: crtOn, name: crtOn ? 'Late Night Studio Sounds Volume Two' : '' });
    for (let op = 1; op <= 6; op++) ev.push(this.opFreq(op));
    ev.push({ e: 'lfoHz', s: '5.21 Hz' });
    ev.push({ e: 'wheels', bend: 8192, mod: 0 });
    ev.push({ e: 'meter', db: [-9, -60, -14, -60, -11, -60] });
    const preset = o.preset ?? 'play';
    const edited = preset === 'edited' || preset === 'edit21';
    ev.push({ e: 'memory', selected: 0, srcBank: 0, srcSlot: 2, edited });
    ev.push({ e: 'led', v: 2, dot: edited });
    if (preset === 'edit21') {
      ev.push({ e: 'lcd', l1: 'ALG 5 111111 OP1', l2: 'EG  RATE  1  =96', row: 0, col: 0, cursor: false, lit: true });
      ev.push({ e: 'lamps', num: 1 << 20, mode: 1 << 2, blink: 0 });
      ev.push({ e: 'panel', mode: 'EDIT', page: 21, op: 1, comparing: false, edited, dialog: false, message: false, armed: false });
      ev.push({ e: 'dataEntry', pos: 96 / 99 });
    } else if (preset === 'func17') {
      ev.push({ e: 'lcd', l1: 'FUNCTION CONTROL', l2: 'WHEEL  RANGE=99 ', row: 0, col: 0, cursor: false, lit: true });
      ev.push({ e: 'lamps', num: 1 << 16, mode: 1 << 3, blink: 0 });
      ev.push({ e: 'panel', mode: 'FUNCTION', page: 17, op: 1, comparing: false, edited, dialog: false, message: false, armed: false });
      ev.push({ e: 'dataEntry', pos: 1 });
    } else {
      ev.push({ e: 'lcd', l1: 'INTERNAL VOICE  ', l2: 'INT 2 E.PIANO   ', row: 1, col: 6, cursor: edited, lit: true });
      ev.push({ e: 'lamps', num: 1 << 1, mode: 1 << 0, blink: 0 });
      ev.push({ e: 'panel', mode: 'PLAY', page: 7, op: 1, comparing: false, edited, dialog: false, message: false, armed: false });
      ev.push({ e: 'dataEntry', pos: 0 });
    }
    ev.push({ e: 'snapshotEnd' });
    return ev;
  }
}

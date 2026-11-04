// The single UI store (Svelte 5 runes). It applies core/shell events in order
// and holds the few pieces of UI-only state (overlays, inline notices).
// Values are shown only after `snapshotEnd` (R-S5).
import type {
  BuildKind,
  Command,
  GlobalId,
  MidiPort,
  MidiState,
  NoticeCode,
  PanelModeName,
  SettingId,
  VdxEvent,
} from './protocol';
import { VOICE_PARAMS, functionDefault, GLOBAL_RANGE } from './params';

const BLANK16 = ' '.repeat(16);
export const NOTICE_MS = 4000; // §5.7 inline notices
export const DUMP_WAIT_MS = 10000; // §5.7 / F-12.6

export type Overlay = 'none' | 'bank' | 'settings';
export type DumpWait = 'idle' | 'waiting' | 'noReply';

interface Value {
  v: number;
  s: string;
}

function initialGlobals(): Record<GlobalId, Value> {
  const g = {} as Record<GlobalId, Value>;
  for (const id of Object.keys(GLOBAL_RANGE) as GlobalId[]) g[id] = { v: GLOBAL_RANGE[id].def, s: '' };
  return g;
}

export class VdxStore {
  /** True once the first snapshot has been applied completely (R-S5). */
  ready = $state(false);
  params = $state<number[]>(new Array(VOICE_PARAMS).fill(0));
  paramStr = $state<string[]>(new Array(VOICE_PARAMS).fill(''));
  /** Function parameters indexed by number 64..77. */
  func = $state<number[]>(Array.from({ length: 78 }, (_, n) => functionDefault(n)));
  funcStr = $state<string[]>(new Array(78).fill(''));
  globals = $state<Record<GlobalId, Value>>(initialGlobals());
  settings = $state<Record<SettingId, number>>({
    engine: 0,
    dac: 1,
    hostRate: 0,
    classicVelocity: 0,
    keyboardPlay: 1,
    zoom: 0,
    opOrder: 0,
  });
  /** True once the core has reported the keyboardPlay setting. */
  keyboardPlayKnown = $state(false);
  lcd = $state({ l1: BLANK16, l2: BLANK16, row: 0, col: 0, cursor: false, lit: false });
  led = $state({ v: 0, dot: false });
  lamps = $state({ num: 0, mode: 0, blink: 0 });
  panel = $state({
    mode: 'PLAY' as PanelModeName,
    page: 0,
    op: 1,
    comparing: false,
    edited: false,
    dialog: false,
    message: false,
    armed: false,
  });
  dataEntry = $state(0);
  wheels = $state({ bend: 8192, mod: 0 });
  /** Bank slot names: names[bank][slot - 1]. */
  names = $state<string[][]>([new Array(32).fill(''), new Array(32).fill('')]);
  cartridge = $state({ present: false, changed: false, name: '' });
  memory = $state({ selected: 0, srcBank: 0, srcSlot: 1, edited: false });
  /** opFreq[op] for op 1..6 (index 0 unused). */
  opFreq = $state<string[]>(new Array(7).fill(''));
  lfoHz = $state('');
  /** Meter levels in dBFS, index 0..5 = OP1..OP6. */
  meter = $state<number[]>(new Array(6).fill(-60));
  meterSeq = $state(0);
  env = $state({
    build: 'web' as BuildKind,
    midi: 'ok' as MidiState,
    inputs: [] as MidiPort[],
    outputs: [] as MidiPort[],
    output: null as string | null,
    idb: true,
    audio: true,
  });
  envKnown = $state(false);

  // ---- UI-only state ---------------------------------------------------
  overlay = $state<Overlay>('none');
  /** Bank-manager inline notice (protected / rejected / write error), 4 s. */
  bankNotice = $state<{ id: number; code: NoticeCode; text: string } | null>(null);
  /** Pending bank import choice (F-12.1). */
  importChoice = $state<{ name: string } | null>(null);
  dumpWait = $state<DumpWait>('idle');
  /** Settings status line (F-15: "Could not save state: ‹reason›"). */
  saveError = $state<string | null>(null);
  /** 400 ms blink phase shared by the LCD cursor and blinking lamps. */
  blinkOn = $state(true);

  private noticeSeq = 0;
  private noticeTimer: ReturnType<typeof setTimeout> | null = null;
  private dumpTimer: ReturnType<typeof setTimeout> | null = null;

  // ---- Event application -------------------------------------------------
  apply(events: readonly VdxEvent[]): void {
    for (const ev of events) this.applyOne(ev);
  }

  applyOne(ev: VdxEvent): void {
    switch (ev.e) {
      case 'param':
        if (ev.i >= 0 && ev.i < VOICE_PARAMS) {
          this.params[ev.i] = ev.v;
          this.paramStr[ev.i] = ev.s ?? '';
        }
        break;
      case 'func':
        if (ev.i >= 64 && ev.i <= 77) {
          this.func[ev.i] = ev.v;
          this.funcStr[ev.i] = ev.s ?? '';
        }
        break;
      case 'global':
        if (ev.id in this.globals) this.globals[ev.id] = { v: ev.v, s: ev.s ?? '' };
        break;
      case 'setting':
        if (ev.id in this.settings) this.settings[ev.id] = ev.v;
        if (ev.id === 'keyboardPlay') this.keyboardPlayKnown = true;
        break;
      case 'lcd':
        this.lcd = {
          l1: (ev.l1 ?? '').padEnd(16).slice(0, 16),
          l2: (ev.l2 ?? '').padEnd(16).slice(0, 16),
          row: ev.row ?? 0,
          col: ev.col ?? 0,
          cursor: !!ev.cursor,
          lit: ev.lit !== false,
        };
        break;
      case 'led':
        this.led = { v: ev.v, dot: !!ev.dot };
        break;
      case 'lamps':
        this.lamps = { num: ev.num >>> 0, mode: ev.mode >>> 0, blink: ev.blink >>> 0 };
        break;
      case 'panel':
        this.panel = {
          mode: ev.mode,
          page: ev.page,
          op: ev.op,
          comparing: !!ev.comparing,
          edited: !!ev.edited,
          dialog: !!ev.dialog,
          message: !!ev.message,
          armed: !!ev.armed,
        };
        break;
      case 'dataEntry':
        this.dataEntry = ev.pos;
        break;
      case 'wheels':
        this.wheels = { bend: ev.bend, mod: ev.mod };
        break;
      case 'voiceName':
        if ((ev.bank === 0 || ev.bank === 1) && ev.slot >= 1 && ev.slot <= 32) this.names[ev.bank][ev.slot - 1] = ev.name;
        break;
      case 'cartridge':
        this.cartridge = { present: !!ev.present, changed: !!ev.changed, name: ev.name ?? '' };
        break;
      case 'memory':
        this.memory = { selected: ev.selected, srcBank: ev.srcBank, srcSlot: ev.srcSlot, edited: !!ev.edited };
        break;
      case 'opFreq':
        if (ev.op >= 1 && ev.op <= 6) this.opFreq[ev.op] = ev.s;
        break;
      case 'lfoHz':
        this.lfoHz = ev.s;
        break;
      case 'meter':
        for (let k = 0; k < 6; k++) this.meter[k] = Math.max(-60, Math.min(0, ev.db[k] ?? -60));
        this.meterSeq++;
        break;
      case 'notice':
        this.applyNotice(ev.code, ev.bank, ev.name, ev.text);
        break;
      case 'import':
        if (ev.kind === 'bank') this.importChoice = { name: ev.name };
        else if (ev.kind === 'reject') this.showBankNotice('rejected', rejectedText(ev.name));
        break;
      case 'snapshotEnd':
        this.ready = true;
        break;
      case 'env':
        this.env = {
          build: ev.build,
          midi: ev.midi,
          inputs: ev.inputs ?? [],
          outputs: ev.outputs ?? [],
          output: ev.output ?? null,
          idb: ev.idb !== false,
          audio: ev.audio !== false,
        };
        this.envKnown = true;
        break;
    }
  }

  private applyNotice(code: NoticeCode, bank?: number, name?: string, text?: string): void {
    switch (code) {
      case 'protected':
        this.showBankNotice(code, bank === 1 ? 'Cartridge memory is protected.' : 'Internal memory is protected.');
        break;
      case 'rejected':
        this.showBankNotice(code, rejectedText(name ?? text ?? ''));
        break;
      case 'writeError':
        this.showBankNotice(code, `Could not write ${text ?? name ?? ''}`);
        break;
      case 'saveError':
        this.saveError = `Could not save state: ${text ?? ''}`;
        break;
      case 'dumpReceived':
        this.endDumpWait('idle');
        break;
      case 'noReply':
        this.endDumpWait('noReply');
        break;
      case 'completed':
        break;
    }
  }

  showBankNotice(code: NoticeCode, text: string): void {
    const id = ++this.noticeSeq;
    this.bankNotice = { id, code, text };
    if (this.noticeTimer) clearTimeout(this.noticeTimer);
    this.noticeTimer = setTimeout(() => {
      if (this.bankNotice?.id === id) this.bankNotice = null;
    }, NOTICE_MS);
  }

  /** Request dump: "Waiting for bank…" for up to 10 s, then "No reply." (UI-side timer). */
  startDumpWait(): void {
    if (this.dumpTimer) clearTimeout(this.dumpTimer);
    this.dumpWait = 'waiting';
    this.dumpTimer = setTimeout(() => {
      if (this.dumpWait === 'waiting') this.dumpWait = 'noReply';
      this.dumpTimer = null;
    }, DUMP_WAIT_MS);
  }

  endDumpWait(next: DumpWait): void {
    if (this.dumpTimer) clearTimeout(this.dumpTimer);
    this.dumpTimer = null;
    if (next === 'noReply' && this.dumpWait !== 'waiting') return;
    this.dumpWait = next;
  }

  /** Optimistic local effect of a UI command (the core's echo confirms it). */
  applyLocal(cmd: Command): void {
    switch (cmd.t) {
      case 'setParam':
        if (cmd.i >= 0 && cmd.i < VOICE_PARAMS) this.params[cmd.i] = cmd.v;
        break;
      case 'setFunc':
        if (cmd.i >= 64 && cmd.i <= 77) this.func[cmd.i] = cmd.v;
        break;
      case 'setGlobal':
        if (cmd.id in this.globals) this.globals[cmd.id].v = cmd.v;
        break;
      case 'setSetting':
        if (cmd.id in this.settings) this.settings[cmd.id] = cmd.v;
        if (cmd.id === 'keyboardPlay') this.keyboardPlayKnown = true;
        break;
      case 'cc':
        if (cmd.cc === 1) this.wheels.mod = cmd.v;
        break;
      case 'bend':
        this.wheels.bend = cmd.v;
        break;
    }
  }

  /** Edit-buffer name (10 characters) from the name parameters 145..154. */
  get voiceName(): string {
    let s = '';
    for (let k = 0; k < 10; k++) {
      const c = this.params[145 + k];
      s += c >= 0x20 && c <= 0x7e ? String.fromCharCode(c) : ' ';
    }
    return s;
  }
}

export function rejectedText(name: string): string {
  // §5.7 text without the brand (R-L5 / AC-L5.1; see DECISIONS.md).
  return `Not a voice or bank file: ${name}`;
}

export const store = new VdxStore();

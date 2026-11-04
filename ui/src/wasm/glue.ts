// Web build glue (SPEC §5.9, §15.2, F-1.3, F-15.3, R-D5): loads the WASM
// core, restores state from IndexedDB, shows the start card, starts audio in
// an AudioWorklet on the first gesture, connects Web MIDI, saves state, and
// installs the WASM backend for the UI bridge.
import { setBackend, type Backend } from '../bridge';
import type { Command, VdxEvent } from '../protocol';
import { showFatalError, showStartCard } from '../web/webstate.svelte';

interface VdxModule {
  HEAPU8: Uint8Array;
  UTF8ToString(ptr: number): string;
  stringToNewUTF8(s: string): number;
  _malloc(n: number): number;
  _free(ptr: number): void;
  emscriptenRegisterAudioObject(o: unknown): number;
  emscriptenGetAudioObject(h: number): unknown;
  onVdxNode?: (handle: number) => void;
  _vdx_init(rate: number): void;
  _vdx_prepare(rate: number): void;
  _vdx_command(json: number): number;
  _vdx_poll(): number;
  _vdx_state_dirty(): number;
  _vdx_save_state(): number;
  _vdx_state_ptr(): number;
  _vdx_load_state(ptr: number, len: number): number;
  _vdx_export_count(): number;
  _vdx_export_ptr(i: number): number;
  _vdx_export_len(i: number): number;
  _vdx_export_name(i: number): number;
  _vdx_export_kind(i: number): number;
  _vdx_export_clear(): void;
  _vdx_midi_in(ptr: number, len: number): void;
  _vdx_midi_out(ptr: number, cap: number): number;
  _vdx_pump(frames: number): void;
  _vdx_start_audio(ctx: number): void;
  _vdx_audio_state(): number;
}

type CreateModule = (opts?: Record<string, unknown>) => Promise<VdxModule>;

declare global {
  interface Window {
    createVdxModule?: CreateModule;
    __vdxTap?: { rmsDb(): number; sampleRate: number; latency(): number };
  }
}

export const MESSAGES = {
  isolation: 'VDX needs to be opened from http://localhost:5173 (cross-origin isolation is off).',
  worklet: 'VDX needs a browser with AudioWorklet support — use a current Chrome, Edge or Safari 17+.',
  wasm: 'VDX could not load its sound engine (vdx.wasm). Build it with web/build.sh, then reload the page.',
} as const;

const INTERNAL_RATE = 49096;
const DB_NAME = 'vdx';
const STORE = 'state';
const EXPORT_KIND_NAMES = ['int', 'crt', 'voice'] as const;

// ---- IndexedDB (R-D5) -----------------------------------------------------

function openDb(): Promise<IDBDatabase | null> {
  return new Promise((resolve) => {
    try {
      if (typeof indexedDB === 'undefined' || !indexedDB) return resolve(null);
      const req = indexedDB.open(DB_NAME, 1);
      req.onupgradeneeded = () => {
        if (!req.result.objectStoreNames.contains(STORE)) req.result.createObjectStore(STORE);
      };
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => resolve(null);
      req.onblocked = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
}

function idbGet(db: IDBDatabase, key: string): Promise<unknown> {
  return new Promise((resolve) => {
    try {
      const req = db.transaction(STORE, 'readonly').objectStore(STORE).get(key);
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => resolve(undefined);
    } catch {
      resolve(undefined);
    }
  });
}

function idbPut(db: IDBDatabase, key: string, value: unknown): Promise<boolean> {
  return new Promise((resolve) => {
    try {
      const tx = db.transaction(STORE, 'readwrite');
      tx.objectStore(STORE).put(value, key);
      tx.oncomplete = () => resolve(true);
      tx.onerror = () => resolve(false);
      tx.onabort = () => resolve(false);
    } catch {
      resolve(false);
    }
  });
}

// ---- module loading ---------------------------------------------------------

function loadScript(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.async = true;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`cannot load ${src}`));
    document.head.appendChild(s);
  });
}

function bytesToBase64(bytes: Uint8Array): string {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

// ---- the glue -----------------------------------------------------------------

interface MidiPortInfo {
  id: string;
  name: string;
  on: boolean;
}

class WebGlue implements Backend {
  readonly kind = 'wasm' as const;
  private m: VdxModule;
  private db: IDBDatabase | null;
  private listener: ((events: VdxEvent[]) => void) | null = null;
  private started = false;
  private audioRunning = false;
  private audioOk = true;
  private ctx: AudioContext | null = null;
  private analyser: AnalyserNode | null = null;
  private lastPump = performance.now();
  private pumpRate = INTERNAL_RATE;
  private heldLcd: VdxEvent | null = null;
  private heldLed: VdxEvent | null = null;
  private saveTimer: ReturnType<typeof setTimeout> | null = null;
  private firstDirty = 0;
  private midiAccess: MIDIAccess | null = null;
  private midiState: 'ok' | 'unavailable' | 'nosysex' = 'unavailable';
  private enabledInputs: Set<string> | null = null;
  private outputId: string | null = null;
  private midiOutBuf = 0;
  private readonly test: boolean;

  constructor(m: VdxModule, db: IDBDatabase | null, devices: unknown, test: boolean) {
    this.m = m;
    this.db = db;
    this.test = test;
    const d = devices as { inputs?: string[] | null; output?: string | null } | undefined;
    if (d && Array.isArray(d.inputs)) this.enabledInputs = new Set(d.inputs);
    if (d && typeof d.output === 'string') this.outputId = d.output;
    this.midiOutBuf = m._malloc(65536);
    setInterval(() => this.tick(), 16);
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') void this.saveNow();
    });
    window.addEventListener('pagehide', () => void this.saveNow());
  }

  // ---- Backend ----
  onEvents(cb: (events: VdxEvent[]) => void): void {
    this.listener = cb;
  }

  send(cmd: Command): void {
    switch (cmd.t) {
      case 'pickFile':
        this.pickFile();
        return;
      case 'midiSelect':
        this.enabledInputs = new Set(cmd.inputs);
        this.outputId = cmd.output;
        this.applyMidiInputs();
        if (this.db) void idbPut(this.db, 'devices', { inputs: [...this.enabledInputs], output: this.outputId });
        this.emitEnv();
        return;
      case 'uiReady':
        this.emitEnv();
        return;
      case 'editorClosing':
        return;
    }
    this.command(cmd);
  }

  command(cmd: object): void {
    const p = this.m.stringToNewUTF8(JSON.stringify(cmd));
    const reply = this.m.UTF8ToString(this.m._vdx_command(p));
    this.m._free(p);
    if (reply && reply !== '[]') this.deliver(JSON.parse(reply) as VdxEvent[]);
  }

  // ---- events ----
  private deliver(events: VdxEvent[]): void {
    if (!this.listener || events.length === 0) return;
    if (!this.started) {
      // Before the first gesture the LCD is unlit and the LED blank (§5.9).
      events = events.map((e) => {
        if (e.e === 'lcd') {
          this.heldLcd = e;
          return { ...e, lit: false, cursor: false };
        }
        if (e.e === 'led') {
          this.heldLed = e;
          return { ...e, v: 0, dot: false };
        }
        return e;
      });
    }
    this.listener(events);
  }

  private tick(): void {
    const now = performance.now();
    if (!this.audioRunning) {
      const frames = Math.min(16384, Math.round(((now - this.lastPump) / 1000) * this.pumpRate));
      if (frames > 0) this.m._vdx_pump(frames);
    }
    this.lastPump = now;
    const json = this.m.UTF8ToString(this.m._vdx_poll());
    if (json) this.deliver(JSON.parse(json) as VdxEvent[]);
    this.handleExports();
    this.flushMidiOut();
    if (this.m._vdx_state_dirty()) this.scheduleSave();
  }

  private emitEnv(): void {
    const inputs: MidiPortInfo[] = [];
    const outputs: { id: string; name: string }[] = [];
    if (this.midiAccess) {
      this.midiAccess.inputs.forEach((i) =>
        inputs.push({ id: i.id, name: i.name ?? i.id, on: !this.enabledInputs || this.enabledInputs.has(i.id) }),
      );
      this.midiAccess.outputs.forEach((o) => outputs.push({ id: o.id, name: o.name ?? o.id }));
    }
    if (this.outputId && !outputs.some((o) => o.id === this.outputId)) this.outputId = null;
    this.deliver([
      {
        e: 'env',
        build: 'web',
        midi: this.midiState,
        inputs,
        outputs,
        output: this.outputId,
        idb: this.db !== null,
        audio: this.audioOk,
      },
    ]);
  }

  // ---- persistence (F-15.3) ----
  private scheduleSave(): void {
    const now = performance.now();
    if (!this.saveTimer) this.firstDirty = now;
    if (this.saveTimer) clearTimeout(this.saveTimer);
    const wait = Math.max(0, Math.min(1000, 5000 - (now - this.firstDirty)));
    this.saveTimer = setTimeout(() => void this.saveNow(), wait);
  }

  async saveNow(): Promise<void> {
    if (this.saveTimer) clearTimeout(this.saveTimer);
    this.saveTimer = null;
    if (!this.db) return;
    const n = this.m._vdx_save_state();
    const ptr = this.m._vdx_state_ptr();
    const copy = this.m.HEAPU8.slice(ptr, ptr + n);
    await idbPut(this.db, 'current', copy.buffer);
  }

  loadState(blob: ArrayBuffer | Uint8Array | undefined): void {
    if (!blob) return;
    const bytes = blob instanceof Uint8Array ? blob : new Uint8Array(blob);
    const ptr = this.m._malloc(bytes.length || 1);
    this.m.HEAPU8.set(bytes, ptr);
    this.m._vdx_load_state(ptr, bytes.length);
    this.m._free(ptr);
  }

  // ---- exports (F-12.5: a download) ----
  private handleExports(): void {
    const n = this.m._vdx_export_count();
    if (n === 0) return;
    for (let i = 0; i < n; ++i) {
      const ptr = this.m._vdx_export_ptr(i);
      const len = this.m._vdx_export_len(i);
      const name = this.m.UTF8ToString(this.m._vdx_export_name(i));
      const kind = EXPORT_KIND_NAMES[this.m._vdx_export_kind(i)] ?? 'int';
      const blob = new Blob([this.m.HEAPU8.slice(ptr, ptr + len)], { type: 'application/octet-stream' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = name;
      a.style.display = 'none';
      document.body.appendChild(a);
      a.click();
      setTimeout(() => {
        URL.revokeObjectURL(a.href);
        a.remove();
      }, 1000);
      this.command({ t: 'exportDone', what: kind });
    }
    this.m._vdx_export_clear();
  }

  // ---- files (F-12.1) ----
  private pickFile(): void {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.syx';
    input.style.display = 'none';
    input.addEventListener('change', () => {
      const f = input.files?.[0];
      input.remove();
      if (f) void this.importFile(f);
    });
    document.body.appendChild(input);
    input.click();
  }

  async importFile(f: File): Promise<void> {
    if (f.size > 1024 * 1024 + 1) {
      // R-D4: larger than 1 MiB is rejected without reading it all
      this.command({ t: 'importFile', name: f.name, b64: '' });
      return;
    }
    const bytes = new Uint8Array(await f.arrayBuffer());
    this.command({ t: 'importFile', name: f.name, b64: bytesToBase64(bytes) });
  }

  // ---- audio (R-E1 Web, F-1.3) ----
  async startAudio(): Promise<void> {
    let ctx: AudioContext;
    try {
      ctx = new AudioContext({ sampleRate: INTERNAL_RATE, latencyHint: 'interactive' });
    } catch {
      try {
        ctx = new AudioContext({ latencyHint: 'interactive' });
      } catch {
        this.audioFailed();
        return;
      }
    }
    this.ctx = ctx;
    this.m._vdx_prepare(ctx.sampleRate);
    this.pumpRate = ctx.sampleRate;
    if (this.test) {
      this.analyser = ctx.createAnalyser();
      this.analyser.fftSize = 4096;
      const an = this.analyser;
      window.__vdxTap = {
        sampleRate: ctx.sampleRate,
        latency: () => 0,
        rmsDb: () => {
          const buf = new Float32Array(an.fftSize);
          an.getFloatTimeDomainData(buf);
          let s = 0;
          for (const x of buf) s += x * x;
          return 10 * Math.log10(s / buf.length + 1e-30);
        },
      };
    }
    this.m.onVdxNode = (handle) => {
      const node = this.m.emscriptenGetAudioObject(handle) as AudioNode;
      if (this.analyser) node.connect(this.analyser);
    };
    try {
      const resumed = ctx.resume();
      this.m._vdx_start_audio(this.m.emscriptenRegisterAudioObject(ctx));
      await resumed;
    } catch {
      this.audioFailed();
      return;
    }
    for (let i = 0; i < 100 && this.m._vdx_audio_state() === 1; ++i) await new Promise((r) => setTimeout(r, 30));
    if (this.m._vdx_audio_state() === 2 && ctx.state === 'running') {
      this.audioRunning = true;
    } else {
      this.audioFailed();
    }
  }

  private audioFailed(): void {
    // "No audio output available." — the core keeps running on the main thread.
    this.audioOk = false;
    this.audioRunning = false;
    this.emitEnv();
  }

  // ---- Web MIDI (§5.8) ----
  async startMidi(): Promise<void> {
    const nav = navigator as Navigator & { requestMIDIAccess?: (o?: { sysex?: boolean }) => Promise<MIDIAccess> };
    if (typeof nav.requestMIDIAccess !== 'function') {
      this.midiState = 'unavailable';
      this.emitEnv();
      return;
    }
    try {
      this.midiAccess = await nav.requestMIDIAccess({ sysex: true });
      this.midiState = this.midiAccess.sysexEnabled === false ? 'nosysex' : 'ok';
    } catch {
      try {
        this.midiAccess = await nav.requestMIDIAccess({ sysex: false });
        this.midiState = 'nosysex';
      } catch {
        this.midiAccess = null;
        this.midiState = 'unavailable';
      }
    }
    if (this.midiAccess) {
      this.midiAccess.onstatechange = () => {
        this.applyMidiInputs();
        this.emitEnv();
      };
      this.applyMidiInputs();
    }
    this.emitEnv();
  }

  private applyMidiInputs(): void {
    if (!this.midiAccess) return;
    this.midiAccess.inputs.forEach((input) => {
      const on = !this.enabledInputs || this.enabledInputs.has(input.id);
      input.onmidimessage = on ? (ev: MIDIMessageEvent) => this.midiIn(ev.data) : null;
    });
  }

  private midiIn(data: Uint8Array | null): void {
    if (!data || data.length === 0) return;
    const ptr = this.m._malloc(data.length);
    this.m.HEAPU8.set(data, ptr);
    this.m._vdx_midi_in(ptr, data.length);
    this.m._free(ptr);
  }

  private flushMidiOut(): void {
    const n = this.m._vdx_midi_out(this.midiOutBuf, 65536);
    if (n <= 0) return;
    const out = this.outputId && this.midiAccess ? this.midiAccess.outputs.get(this.outputId) : undefined;
    if (!out) return;
    const bytes = this.m.HEAPU8.slice(this.midiOutBuf, this.midiOutBuf + n);
    // send complete messages one by one (SysEx needs permission)
    let i = 0;
    while (i < bytes.length) {
      let j = i + 1;
      if (bytes[i] === 0xf0) {
        while (j < bytes.length && bytes[j - 1] !== 0xf7) ++j;
      } else {
        while (j < bytes.length && bytes[j] < 0x80) ++j;
      }
      try {
        out.send(bytes.subarray(i, j));
      } catch {
        // SysEx not permitted or port closed: drop silently
      }
      i = j;
    }
  }

  // ---- start gesture ----
  onStartGesture(): void {
    this.started = true;
    void this.startAudio();
    void this.startMidi();
    this.command({ t: 'startSplash' });
    const held = [this.heldLcd, this.heldLed].filter((e): e is VdxEvent => e !== null);
    if (held.length && this.listener) this.listener(held);
  }
}

/** Boots the web build. Returns the glue (null after a fatal error). */
export async function startWeb(opts: { test?: boolean } = {}): Promise<WebGlue | null> {
  if (!globalThis.crossOriginIsolated) {
    showFatalError(MESSAGES.isolation);
    return null;
  }
  if (typeof AudioWorkletNode === 'undefined' || typeof AudioContext === 'undefined') {
    showFatalError(MESSAGES.worklet);
    return null;
  }
  let m: VdxModule;
  try {
    await loadScript(`${import.meta.env.BASE_URL}vdx.js`);
    if (!window.createVdxModule) throw new Error('createVdxModule missing');
    m = await window.createVdxModule();
  } catch {
    showFatalError(MESSAGES.wasm);
    return null;
  }
  m._vdx_init(INTERNAL_RATE);
  const db = await openDb();
  const devices = db ? await idbGet(db, 'devices') : undefined;
  const glue = new WebGlue(m, db, devices, opts.test === true);
  if (db) glue.loadState((await idbGet(db, 'current')) as ArrayBuffer | undefined);
  setBackend(glue);
  // Dropping a .syx file anywhere on the window (F-12.2) is handled by the UI,
  // which sends importFile itself. The start card waits for the first gesture.
  void showStartCard(() => glue.onStartGesture());
  return glue;
}

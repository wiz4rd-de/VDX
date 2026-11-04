// UI ⇄ core bridge (docs/PROTOCOL.md). The UI talks to the core only through
// this module. A Backend carries JSON-shaped commands to the core (or shell)
// and delivers event batches back; the JUCE backend is built in, the web glue
// installs the WASM backend with setBackend().
import type { Command, VdxEvent } from './protocol';
import { store, type VdxStore } from './store.svelte';

export type BackendKind = 'juce' | 'wasm' | 'test';

export interface Backend {
  /** Which transport this is. */
  readonly kind: BackendKind;
  /** Sends one command (UI → core / shell). Must not throw. */
  send(cmd: Command): void;
  /** Registers the single event consumer; `cb` receives each batch in order. */
  onEvents(cb: (events: VdxEvent[]) => void): void;
}

let backend: Backend | null = null;
const queued: Command[] = [];
let target: VdxStore = store;

/** The connected backend, or null before setBackend(). */
export function currentBackend(): Backend | null {
  return backend;
}

/**
 * Connects a backend: events go to the store, `{t:'snapshot'}` is sent first
 * (R-S5), then every command queued before the connection.
 */
export function setBackend(b: Backend, s: VdxStore = store): void {
  backend = b;
  target = s;
  b.onEvents((events) => target.apply(events));
  b.send({ t: 'snapshot' });
  for (const cmd of queued.splice(0)) b.send(cmd);
}

/** Test hook: forget the backend and pending commands. */
export function resetBridge(): void {
  backend = null;
  queued.length = 0;
  pending.clear();
  frameRequested = false;
  target = store;
}

/** Sends a command now (after flushing any coalesced value for the same key). */
export function send(cmd: Command): void {
  const key = coalesceKey(cmd);
  if (key) pending.delete(key);
  target.applyLocal(cmd);
  deliver(cmd);
}

function deliver(cmd: Command): void {
  if (backend) backend.send(cmd);
  else queued.push(cmd);
}

// ---- Coalescing (≤ 1 message per animation frame per parameter) ---------
const pending = new Map<string, Command>();
let frameRequested = false;

export function coalesceKey(cmd: Command): string | null {
  switch (cmd.t) {
    case 'setParam':
    case 'setFunc':
      return `${cmd.t}:${cmd.i}`;
    case 'setGlobal':
    case 'setSetting':
      return `${cmd.t}:${cmd.id}`;
    case 'cc':
      return `cc:${cmd.cc}`;
    case 'bend':
    case 'dataEntry':
      return cmd.t;
  }
  return null;
}

const requestFrame: (cb: () => void) => void =
  typeof requestAnimationFrame === 'function'
    ? (cb) => requestAnimationFrame(() => cb())
    : (cb) => setTimeout(cb, 16);

/** Queues a continuous change (drag, wheel); the latest value per key goes out on the next frame. */
export function sendSoon(cmd: Command): void {
  const key = coalesceKey(cmd);
  if (!key) {
    send(cmd);
    return;
  }
  target.applyLocal(cmd);
  pending.delete(key);
  pending.set(key, cmd);
  if (!frameRequested) {
    frameRequested = true;
    requestFrame(flush);
  }
}

/** Sends every pending coalesced command now (end of a drag). */
export function flush(): void {
  frameRequested = false;
  if (pending.size === 0) return;
  const cmds = [...pending.values()];
  pending.clear();
  for (const cmd of cmds) deliver(cmd);
}

// ---- JUCE backend (standalone, plugin) ----------------------------------
interface JuceBackendObject {
  emitEvent(eventId: string, payload: unknown): void;
  addEventListener(eventId: string, fn: (payload: unknown) => void): unknown;
}
interface JuceWindow {
  __JUCE__?: {
    backend?: JuceBackendObject;
    initialisationData?: { __juce__functions?: string[] };
  };
}

/** Parses a `vdxEvents` payload (a JSON array string, or an already-parsed array). */
export function parseEventPayload(payload: unknown): VdxEvent[] {
  let value = payload;
  if (typeof value === 'string') {
    try {
      value = JSON.parse(value);
    } catch {
      return [];
    }
  }
  return Array.isArray(value) ? (value as VdxEvent[]) : [];
}

/**
 * JUCE 9 WebBrowserComponent native integration: the native function
 * `vdxCommand` takes one JSON string; the event `vdxEvents` carries a JSON
 * array string. A non-empty return value of `vdxCommand` (immediate replies
 * such as the `import` result) arrives as `__juce__complete` and is delivered
 * like an event batch. Returns null outside a JUCE web view.
 */
export function createJuceBackend(win: unknown = globalThis): Backend | null {
  const juce = (win as JuceWindow).__JUCE__;
  const nativeBackend = juce?.backend;
  const functions = juce?.initialisationData?.__juce__functions ?? [];
  if (!nativeBackend || !functions.includes('vdxCommand')) return null;
  let resultId = 0;
  const awaiting = new Set<number>();
  return {
    kind: 'juce',
    send(cmd) {
      const id = resultId++;
      awaiting.add(id);
      if (awaiting.size > 4096) awaiting.delete(awaiting.values().next().value as number);
      nativeBackend.emitEvent('__juce__invoke', {
        name: 'vdxCommand',
        params: [JSON.stringify(cmd)],
        resultId: id,
      });
    },
    onEvents(cb) {
      nativeBackend.addEventListener('vdxEvents', (payload) => cb(parseEventPayload(payload)));
      nativeBackend.addEventListener('__juce__complete', (payload) => {
        const p = payload as { promiseId?: number; result?: unknown } | null;
        if (!p || typeof p.promiseId !== 'number' || !awaiting.delete(p.promiseId)) return;
        const events = parseEventPayload(p.result);
        if (events.length > 0) cb(events);
      });
    },
  };
}

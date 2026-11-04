import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { setBackend, send, sendSoon, flush, resetBridge, createJuceBackend, parseEventPayload } from '../src/bridge';
import { VdxStore } from '../src/store.svelte';
import type { Command, VdxEvent } from '../src/protocol';
import type { Backend } from '../src/bridge';
import { FakeBackend } from './fake-backend';

function recorder(): Backend & { sent: Command[]; push(e: VdxEvent[]): void } {
  let cb: ((e: VdxEvent[]) => void) | null = null;
  const sent: Command[] = [];
  return {
    kind: 'test',
    sent,
    send: (c) => sent.push(c),
    onEvents: (f) => (cb = f),
    push: (e) => cb?.(e),
  };
}

describe('bridge (docs/PROTOCOL.md)', () => {
  beforeEach(() => resetBridge());

  it('R-S5 sends {t:"snapshot"} on connect, then commands queued before the connection', () => {
    const s = new VdxStore();
    send({ t: 'uiReady' });
    const b = recorder();
    setBackend(b, s);
    expect(b.sent).toEqual([{ t: 'snapshot' }, { t: 'uiReady' }]);
  });

  it('R-S5 the store shows values only after snapshotEnd', () => {
    const s = new VdxStore();
    const b = recorder();
    setBackend(b, s);
    b.push([{ e: 'param', i: 134, v: 4, s: '5' }]);
    expect(s.params[134]).toBe(4);
    expect(s.ready).toBe(false);
    b.push([{ e: 'snapshotEnd' }]);
    expect(s.ready).toBe(true);
  });

  it('coalesces drags to at most one message per animation frame per parameter', async () => {
    const s = new VdxStore();
    const b = recorder();
    setBackend(b, s);
    b.sent.length = 0;
    for (let v = 0; v < 20; v++) sendSoon({ t: 'setParam', i: 16, v });
    sendSoon({ t: 'setParam', i: 17, v: 1 });
    sendSoon({ t: 'bend', v: 100 });
    sendSoon({ t: 'bend', v: 200 });
    expect(b.sent).toEqual([]);
    expect(s.params[16]).toBe(19); // optimistic local value
    await new Promise((r) => setTimeout(r, 40));
    expect(b.sent).toEqual([
      { t: 'setParam', i: 16, v: 19 },
      { t: 'setParam', i: 17, v: 1 },
      { t: 'bend', v: 200 },
    ]);
    sendSoon({ t: 'setParam', i: 16, v: 30 });
    flush();
    expect(b.sent.at(-1)).toEqual({ t: 'setParam', i: 16, v: 30 });
  });

  it('a direct send supersedes a pending coalesced value of the same parameter', async () => {
    const b = recorder();
    setBackend(b, new VdxStore());
    b.sent.length = 0;
    sendSoon({ t: 'setFunc', i: 69, v: 10 });
    send({ t: 'setFunc', i: 69, v: 50 });
    await new Promise((r) => setTimeout(r, 40));
    expect(b.sent).toEqual([{ t: 'setFunc', i: 69, v: 50 }]);
  });

  it('JUCE backend: native function vdxCommand with one JSON string; event vdxEvents with a JSON array string', () => {
    const emitted: { id: string; payload: unknown }[] = [];
    const listeners: Record<string, (p: unknown) => void> = {};
    const win = {
      __JUCE__: {
        initialisationData: { __juce__functions: ['vdxCommand'] },
        backend: {
          emitEvent: (id: string, payload: unknown) => emitted.push({ id, payload }),
          addEventListener: (id: string, fn: (p: unknown) => void) => (listeners[id] = fn),
        },
      },
    };
    const juce = createJuceBackend(win);
    expect(juce?.kind).toBe('juce');
    const s = new VdxStore();
    setBackend(juce!, s);
    send({ t: 'button', id: 7, down: true });
    expect(emitted[0]).toEqual({
      id: '__juce__invoke',
      payload: { name: 'vdxCommand', params: ['{"t":"snapshot"}'], resultId: 0 },
    });
    expect(emitted[1].payload).toEqual({ name: 'vdxCommand', params: ['{"t":"button","id":7,"down":true}'], resultId: 1 });
    listeners.vdxEvents(JSON.stringify([{ e: 'led', v: 7, dot: true }, { e: 'snapshotEnd' }]));
    expect(s.led).toEqual({ v: 7, dot: true });
    expect(s.ready).toBe(true);
    // An immediate reply (the return value of vdxCommand) is applied like an event batch.
    send({ t: 'importFile', name: 'x.syx', b64: '' });
    listeners.__juce__complete({ promiseId: 2, result: '[{"e":"import","kind":"bank","name":"x.syx"}]' });
    expect(s.importChoice).toEqual({ name: 'x.syx' });
    listeners.__juce__complete({ promiseId: 2, result: '[{"e":"led","v":1,"dot":false}]' });
    expect(s.led.v).toBe(7); // each reply is delivered once
    listeners.__juce__complete({ promiseId: 1, result: '' });
    expect(createJuceBackend({})).toBeNull();
    expect(createJuceBackend({ __JUCE__: { backend: win.__JUCE__.backend, initialisationData: { __juce__functions: [] } } })).toBeNull();
  });

  it('parses event payloads given as JSON strings or arrays, ignoring garbage', () => {
    expect(parseEventPayload('[{"e":"snapshotEnd"}]')).toEqual([{ e: 'snapshotEnd' }]);
    expect(parseEventPayload([{ e: 'snapshotEnd' }])).toEqual([{ e: 'snapshotEnd' }]);
    expect(parseEventPayload('not json')).toEqual([]);
    expect(parseEventPayload({})).toEqual([]);
  });
});

describe('store event application', () => {
  afterEach(() => vi.useRealTimers());

  it('applies every event type in order', () => {
    const s = new VdxStore();
    s.apply([
      { e: 'param', i: 0, v: 50, s: '50' },
      { e: 'func', i: 65, v: 12, s: '12' },
      { e: 'global', id: 'masterTune', v: 127, s: '+73.8 cents' },
      { e: 'setting', id: 'keyboardPlay', v: 0 },
      { e: 'lcd', l1: 'INTERNAL VOICE', l2: 'INT 1 INIT VOICE', row: 1, col: 6, cursor: true, lit: true },
      { e: 'lamps', num: 1, mode: 1, blink: 4 },
      { e: 'panel', mode: 'EDIT', page: 21, op: 3, comparing: false, edited: true, dialog: false, message: false, armed: false },
      { e: 'dataEntry', pos: 0.5 },
      { e: 'wheels', bend: 0, mod: 127 },
      { e: 'voiceName', bank: 1, slot: 32, name: 'LAST ONE  ' },
      { e: 'cartridge', present: true, changed: true, name: 'X' },
      { e: 'memory', selected: 1, srcBank: 1, srcSlot: 9, edited: true },
      { e: 'opFreq', op: 3, s: ' 1.00' },
      { e: 'lfoHz', s: '5.21 Hz' },
      { e: 'meter', db: [-10, -20, -30, -40, -50, -99] },
    ]);
    expect(s.params[0]).toBe(50);
    expect(s.funcStr[65]).toBe('12');
    expect(s.globals.masterTune).toEqual({ v: 127, s: '+73.8 cents' });
    expect(s.settings.keyboardPlay).toBe(0);
    expect(s.keyboardPlayKnown).toBe(true);
    expect(s.lcd.l1).toBe('INTERNAL VOICE  ');
    expect(s.lcd.cursor).toBe(true);
    expect(s.lamps.blink).toBe(4);
    expect(s.panel.page).toBe(21);
    expect(s.dataEntry).toBe(0.5);
    expect(s.wheels).toEqual({ bend: 0, mod: 127 });
    expect(s.names[1][31]).toBe('LAST ONE  ');
    expect(s.cartridge.changed).toBe(true);
    expect(s.memory.srcSlot).toBe(9);
    expect(s.opFreq[3]).toBe(' 1.00');
    expect(s.lfoHz).toBe('5.21 Hz');
    expect(s.meter).toEqual([-10, -20, -30, -40, -50, -60]);
  });

  it('§5.7 inline notices last 4 s; protected / rejected / write error texts', () => {
    vi.useFakeTimers();
    const s = new VdxStore();
    s.apply([{ e: 'notice', code: 'protected', bank: 0 }]);
    expect(s.bankNotice?.text).toBe('Internal memory is protected.');
    s.apply([{ e: 'notice', code: 'protected', bank: 1 }]);
    expect(s.bankNotice?.text).toBe('Cartridge memory is protected.');
    vi.advanceTimersByTime(3999);
    expect(s.bankNotice).not.toBeNull();
    vi.advanceTimersByTime(1);
    expect(s.bankNotice).toBeNull();
    s.apply([{ e: 'import', kind: 'reject', name: 'drums.wav' }]);
    expect(s.bankNotice?.text).toBe('Not a voice or bank file: drums.wav');
    s.apply([{ e: 'notice', code: 'writeError', text: '/tmp/x.syx' }]);
    expect(s.bankNotice?.text).toBe('Could not write /tmp/x.syx');
    s.apply([{ e: 'notice', code: 'saveError', text: 'disk full' }]);
    expect(s.saveError).toBe('Could not save state: disk full');
  });

  it('F-12.6 request dump: "Waiting for bank…" up to 10 s, then "No reply."; a received dump ends the wait', () => {
    vi.useFakeTimers();
    const s = new VdxStore();
    s.startDumpWait();
    expect(s.dumpWait).toBe('waiting');
    vi.advanceTimersByTime(9999);
    expect(s.dumpWait).toBe('waiting');
    vi.advanceTimersByTime(1);
    expect(s.dumpWait).toBe('noReply');
    s.startDumpWait();
    s.apply([{ e: 'notice', code: 'dumpReceived' }]);
    expect(s.dumpWait).toBe('idle');
    vi.advanceTimersByTime(20000);
    expect(s.dumpWait).toBe('idle');
  });

  it('F-12.1 a bank import shows the choice', () => {
    const s = new VdxStore();
    s.apply([{ e: 'import', kind: 'bank', name: 'rom1a' }]);
    expect(s.importChoice).toEqual({ name: 'rom1a' });
  });

  it('the test double produces a complete snapshot ending in snapshotEnd', () => {
    const fake = new FakeBackend({ sync: true });
    const s = new VdxStore();
    resetBridge();
    setBackend(fake, s);
    expect(s.ready).toBe(true);
    expect(s.voiceName).toBe('E.PIANO   ');
    expect(s.names[0][1]).toBe('E.PIANO   ');
  });
});

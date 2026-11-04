// Test doubles for the Playwright suites (SPEC §10): a Web MIDI double for
// navigator.requestMIDIAccess that records sent bytes and injects messages,
// an AudioContext stub that refuses 49,096 Hz, and an IndexedDB blocker.
import type { Page } from '@playwright/test';

export type MidiMode = 'ok' | 'nosysex' | 'reject' | 'absent';

/** Installs the Web MIDI double before any page script runs. */
export async function installMidiDouble(page: Page, mode: MidiMode = 'ok'): Promise<void> {
  await page.addInitScript((m: string) => {
    const w = window as unknown as Record<string, unknown>;
    const sent: number[][] = [];
    w.__midiSent = sent;
    if (m === 'absent') {
      Object.defineProperty(navigator, 'requestMIDIAccess', { value: undefined, configurable: true });
      return;
    }
    type Handler = ((ev: { data: Uint8Array }) => void) | null;
    const input = {
      id: 'mock-in',
      name: 'Mock MIDI In',
      manufacturer: 'VDX tests',
      type: 'input',
      state: 'connected',
      connection: 'open',
      onmidimessage: null as Handler,
      addEventListener() {},
      removeEventListener() {},
    };
    const output = {
      id: 'mock-out',
      name: 'Mock MIDI Out',
      manufacturer: 'VDX tests',
      type: 'output',
      state: 'connected',
      connection: 'open',
      send(data: ArrayLike<number>) {
        sent.push(Array.from(data));
      },
      addEventListener() {},
      removeEventListener() {},
    };
    w.__midiInject = (bytes: number[]) => {
      if (input.onmidimessage) input.onmidimessage({ data: new Uint8Array(bytes) });
    };
    const access = (sysex: boolean) => ({
      inputs: new Map([[input.id, input]]),
      outputs: new Map([[output.id, output]]),
      sysexEnabled: sysex,
      onstatechange: null,
      addEventListener() {},
      removeEventListener() {},
    });
    Object.defineProperty(navigator, 'requestMIDIAccess', {
      configurable: true,
      value: (opts?: { sysex?: boolean }) => {
        if (m === 'reject') return Promise.reject(new DOMException('denied', 'SecurityError'));
        if (m === 'nosysex' && opts?.sysex) return Promise.reject(new DOMException('sysex denied', 'SecurityError'));
        return Promise.resolve(access(m === 'ok'));
      },
    });
  }, mode);
}

/** Makes `new AudioContext({ sampleRate: 49096 })` throw (AC-E1.3). */
export async function refuseInternalRate(page: Page): Promise<void> {
  await page.addInitScript(() => {
    const Real = window.AudioContext;
    class Stub extends Real {
      constructor(opts?: AudioContextOptions) {
        if (opts && opts.sampleRate === 49096) throw new DOMException('rate not supported', 'NotSupportedError');
        super(opts);
      }
    }
    Object.defineProperty(window, 'AudioContext', { value: Stub, configurable: true, writable: true });
  });
}

/** Makes IndexedDB unavailable (AC-F15.2). */
export async function disableIndexedDb(page: Page): Promise<void> {
  await page.addInitScript(() => {
    Object.defineProperty(window, 'indexedDB', {
      configurable: true,
      get() {
        return {
          open() {
            throw new DOMException('IndexedDB disabled', 'InvalidStateError');
          },
        };
      },
    });
  });
}

/** Bytes the app sent to the mocked MIDI output. */
export async function midiSent(page: Page): Promise<number[][]> {
  return page.evaluate(() => (window as unknown as { __midiSent: number[][] }).__midiSent.slice());
}

/** Injects one MIDI message into the mocked input. */
export async function midiInject(page: Page, bytes: number[]): Promise<void> {
  await page.evaluate((b) => (window as unknown as { __midiInject(x: number[]): void }).__midiInject(b), bytes);
}

/** Analyser RMS in dBFS (test builds expose window.__vdxTap). */
export async function rmsDb(page: Page): Promise<number> {
  return page.evaluate(() => (window as unknown as { __vdxTap?: { rmsDb(): number } }).__vdxTap?.rmsDb() ?? -999);
}

import { describe, it, expect, beforeEach } from 'vitest';
import {
  ComputerKeyboard,
  NOTE_KEYS,
  MOD_KEYS,
  BEND_CENTRE,
  type KeyEventLike,
  type KeyboardOptions,
} from '../src/keyboard';
import type { Command } from '../src/protocol';

interface Ev extends KeyEventLike {
  prevented: boolean;
}

function ev(code: string, extra: Partial<KeyEventLike> = {}): Ev {
  const e: Ev = {
    code,
    prevented: false,
    ...extra,
    preventDefault() {
      e.prevented = true;
    },
  };
  return e;
}

const TEXT = { kind: 'text-field' };
const NUMERIC = { kind: 'numeric-control' };

function setup(opts: Partial<KeyboardOptions> = {}) {
  const sent: Command[] = [];
  const kb = new ComputerKeyboard({
    send: (c) => sent.push(c),
    isTextField: (t) => t === TEXT,
    isNumberEntry: (t) => t === NUMERIC,
    ...opts,
  });
  const press = (code: string, extra: Partial<KeyEventLike> = {}) => {
    const e = ev(code, extra);
    kb.keydown(e);
    return e;
  };
  const release = (code: string, extra: Partial<KeyEventLike> = {}) => {
    const e = ev(code, extra);
    kb.keyup(e);
    return e;
  };
  return { kb, sent, press, release };
}

const WHITE: [string, number][] = [
  ['KeyA', 60], ['KeyS', 62], ['KeyD', 64], ['KeyF', 65], ['KeyG', 67], ['KeyH', 69],
  ['KeyJ', 71], ['KeyK', 72], ['KeyL', 74], ['Semicolon', 76], ['Quote', 77],
];
const BLACK: [string, number][] = [
  ['KeyW', 61], ['KeyE', 63], ['KeyT', 66], ['KeyY', 68], ['KeyU', 70], ['KeyO', 73], ['KeyP', 75],
];

describe('AC-K1.1 computer keyboard map (play on)', () => {
  let t: ReturnType<typeof setup>;
  beforeEach(() => {
    t = setup({ build: 'web' });
  });

  it('AC-K1.1 white keys A S D F G H J K L ; \' play C D E F G A B C D E F from C3 = MIDI 60', () => {
    for (const [code, note] of WHITE) {
      t.sent.length = 0;
      const d = t.press(code);
      expect(d.prevented).toBe(true);
      expect(t.sent).toEqual([{ t: 'noteOn', note, vel: 100 }]);
      t.release(code);
      expect(t.sent[1]).toEqual({ t: 'noteOff', note });
    }
  });

  it('AC-K1.1 black keys W E T Y U O P play C# D# F# G# A# C# D#', () => {
    for (const [code, note] of BLACK) {
      t.sent.length = 0;
      t.press(code);
      t.release(code);
      expect(t.sent).toEqual([{ t: 'noteOn', note, vel: 100 }, { t: 'noteOff', note }]);
    }
    expect(Object.keys(NOTE_KEYS)).toHaveLength(18);
  });

  it('AC-K1.1 matches KeyboardEvent.code, never the character', () => {
    // A layout that types "q" on the KeyA position still plays C3.
    t.kb.keydown({ code: 'KeyA', key: 'q' } as KeyEventLike);
    expect(t.sent).toEqual([{ t: 'noteOn', note: 60, vel: 100 }]);
  });

  it('AC-K1.1 Z / X move the base in octaves within 0…108', () => {
    t.press('KeyZ');
    t.press('KeyA');
    expect(t.sent.at(-1)).toEqual({ t: 'noteOn', note: 48, vel: 100 });
    for (let k = 0; k < 10; k++) t.press('KeyZ');
    expect(t.kb.base).toBe(0);
    for (let k = 0; k < 20; k++) t.press('KeyX');
    expect(t.kb.base).toBe(108);
    t.press('KeyS');
    expect(t.sent.at(-1)).toEqual({ t: 'noteOn', note: 110, vel: 100 });
  });

  it('AC-K1.1 notes above 127 are not played', () => {
    const big = setup({ build: 'web' });
    big.kb.base = 120; // beyond the reachable map, to check the guard itself
    big.press('KeyA');
    big.press('KeyK');
    expect(big.sent).toEqual([{ t: 'noteOn', note: 120, vel: 100 }]);
  });

  it('AC-K1.1 C / V change velocity by 20 from 100, clamped to 1–127', () => {
    t.press('KeyC');
    t.press('KeyA');
    expect(t.sent.at(-1)).toEqual({ t: 'noteOn', note: 60, vel: 80 });
    for (let k = 0; k < 10; k++) t.press('KeyC');
    expect(t.kb.velocity).toBe(1);
    for (let k = 0; k < 10; k++) t.press('KeyV');
    expect(t.kb.velocity).toBe(127);
    t.press('KeyS');
    expect(t.sent.at(-1)).toEqual({ t: 'noteOn', note: 62, vel: 127 });
  });

  it('AC-K1.1 Space holds sustain (CC64 127 / 0)', () => {
    t.press('Space');
    t.release('Space');
    expect(t.sent).toEqual([
      { t: 'cc', cc: 64, v: 127 },
      { t: 'cc', cc: 64, v: 0 },
    ]);
  });

  it('AC-K1.1 1 / 2 hold pitch bend fully down / up and centre on release', () => {
    t.press('Digit1');
    t.release('Digit1');
    t.press('Digit2');
    t.release('Digit2');
    expect(t.sent).toEqual([
      { t: 'bend', v: 0 },
      { t: 'bend', v: BEND_CENTRE },
      { t: 'bend', v: 16383 },
      { t: 'bend', v: BEND_CENTRE },
    ]);
  });

  it('AC-K1.1 3–8 set the mod wheel to 0 / 25 / 51 / 76 / 102 / 127', () => {
    for (const code of ['Digit3', 'Digit4', 'Digit5', 'Digit6', 'Digit7', 'Digit8']) t.press(code);
    expect(t.sent).toEqual([0, 25, 51, 76, 102, 127].map((v) => ({ t: 'cc', cc: 1, v })));
    expect(Object.values(MOD_KEYS)).toEqual([0, 25, 51, 76, 102, 127]);
  });

  it('AC-K1.1 M and F9 toggle keyboard play', () => {
    const changes: boolean[] = [];
    const k = setup({ build: 'web', onPlayChange: (on) => changes.push(on) });
    k.press('KeyM');
    expect(k.kb.play).toBe(false);
    k.press('KeyA');
    expect(k.sent).toEqual([]);
    k.press('F9');
    expect(k.kb.play).toBe(true);
    expect(changes).toEqual([false, true]);
  });
});

describe('AC-K2.1 computer keyboard behaviour (play on)', () => {
  it('AC-K2.1 key repeat: no repeat note-on', () => {
    const t = setup({ build: 'web' });
    t.press('KeyA');
    const r = t.press('KeyA', { repeat: true });
    t.press('KeyA'); // a second keydown without keyup (still held)
    expect(r.prevented).toBe(true);
    expect(t.sent).toEqual([{ t: 'noteOn', note: 60, vel: 100 }]);
  });

  it('AC-K2.1 octave change while held: the original note is released', () => {
    const t = setup({ build: 'web' });
    t.press('KeyA');
    t.press('KeyX');
    t.release('KeyA');
    expect(t.sent).toEqual([
      { t: 'noteOn', note: 60, vel: 100 },
      { t: 'noteOff', note: 60 },
    ]);
  });

  it('AC-K2.1 window blur releases all', () => {
    const t = setup({ build: 'web' });
    t.press('KeyA');
    t.press('KeyD');
    t.press('Space');
    t.press('Digit2');
    t.sent.length = 0;
    t.kb.blur();
    expect(t.sent).toEqual([
      { t: 'noteOff', note: 60 },
      { t: 'noteOff', note: 64 },
      { t: 'cc', cc: 64, v: 0 },
      { t: 'bend', v: BEND_CENTRE },
    ]);
    t.release('KeyA');
    expect(t.sent).toHaveLength(4); // no second note-off
  });

  it('AC-K2.1 text field focus releases all and suspends the map', () => {
    const t = setup({ build: 'web' });
    t.press('KeyA');
    t.sent.length = 0;
    t.kb.textFieldFocused();
    expect(t.sent).toEqual([{ t: 'noteOff', note: 60 }]);
    const typed = t.press('KeyS', { target: TEXT });
    const toggle = t.press('KeyM', { target: TEXT });
    expect(typed.prevented).toBe(false);
    expect(toggle.prevented).toBe(false);
    expect(t.kb.play).toBe(true);
    expect(t.sent).toHaveLength(1);
  });

  it('AC-K2.1 play toggled off releases all', () => {
    const t = setup({ build: 'web' });
    t.press('KeyA');
    t.press('KeyG');
    t.sent.length = 0;
    t.press('KeyM');
    expect(t.sent).toEqual([
      { t: 'noteOff', note: 60 },
      { t: 'noteOff', note: 67 },
    ]);
    const a = t.press('KeyA');
    expect(a.prevented).toBe(false);
    expect(t.sent).toHaveLength(2);
  });

  it('AC-K2.1 Ctrl+A (and Cmd, Alt, Meta) plays no note and is not captured', () => {
    const t = setup({ build: 'web' });
    for (const mod of [{ ctrlKey: true }, { metaKey: true }, { altKey: true }]) {
      const e = t.press('KeyA', mod);
      expect(e.prevented).toBe(false);
    }
    expect(t.sent).toEqual([]);
  });

  it('AC-K2.1 isComposing plays no note', () => {
    const t = setup({ build: 'web' });
    const e = t.press('KeyA', { isComposing: true });
    expect(e.prevented).toBe(false);
    expect(t.sent).toEqual([]);
  });

  it('AC-K2.1 Tab is never captured (focus moves)', () => {
    const t = setup({ build: 'web' });
    const down = t.press('Tab');
    const up = t.release('Tab');
    expect(down.prevented).toBe(false);
    expect(up.prevented).toBe(false);
    expect(t.sent).toEqual([]);
  });

  it('AC-K2.1 Space is sustain (captured) and Enter is left to the focused control', () => {
    const t = setup({ build: 'web' });
    const sp = t.press('Space');
    const spUp = t.release('Space');
    const enter = t.press('Enter');
    expect(sp.prevented).toBe(true);
    expect(spUp.prevented).toBe(true);
    expect(enter.prevented).toBe(false);
    expect(t.sent).toEqual([
      { t: 'cc', cc: 64, v: 127 },
      { t: 'cc', cc: 64, v: 0 },
    ]);
    // With play off, Space is not captured and activates focused buttons as usual.
    t.press('KeyM');
    expect(t.press('Space').prevented).toBe(false);
  });

  it('AC-K2.1 note-off uses the note stored at key-down even with modifiers held at release', () => {
    const t = setup({ build: 'web' });
    t.press('KeyJ');
    t.release('KeyJ', { ctrlKey: true });
    expect(t.sent).toEqual([
      { t: 'noteOn', note: 71, vel: 100 },
      { t: 'noteOff', note: 71 },
    ]);
  });

  it('AC-K2.1 a focused numeric control receives digits for typed entry (R-U1)', () => {
    const t = setup({ build: 'web' });
    const d = t.press('Digit5', { target: NUMERIC });
    expect(d.prevented).toBe(false);
    t.press('KeyA', { target: NUMERIC });
    expect(t.sent).toEqual([{ t: 'noteOn', note: 60, vel: 100 }]);
  });
});

describe('AC-K2.2 plugin default', () => {
  it('AC-K2.2 plugin build: play is off at first open; M turns it on', () => {
    const t = setup({ build: 'plugin' });
    expect(t.kb.play).toBe(false);
    const a = t.press('KeyA');
    expect(a.prevented).toBe(false);
    expect(t.sent).toEqual([]);
    t.press('KeyM');
    expect(t.kb.play).toBe(true);
    t.press('KeyA');
    expect(t.sent).toEqual([{ t: 'noteOn', note: 60, vel: 100 }]);
  });

  it('AC-K2.2 standalone and web default to play on', () => {
    expect(setup({ build: 'standalone' }).kb.play).toBe(true);
    expect(setup({ build: 'web' }).kb.play).toBe(true);
  });
});

import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  parseEntry,
  wheelDelta,
  dragSteps,
  DragTracker,
  isEntryStartKey,
  step,
} from '../src/controls/numeric';
import { sanitizeName, padName, nameCodes } from '../src/name';
import { MSFA_ALGORITHMS, ALGORITHMS, algorithmGroup } from '../src/programmer/algorithms';
import { FONT5X7, glyphRows, cellDots } from '../src/lcd/font5x7';
import { ledSegments, DIGIT_SEGMENTS } from '../src/led/segments';
import { fitScale } from '../src/scale';
import { midEllipsis, midiOutBlocked, toBase64 } from '../src/bank/files';
import {
  voiceMax,
  functionMax,
  opParam,
  opOfIndex,
  OP,
  G,
  maskBit,
  INIT_OPERATOR,
  voiceEntryOffset,
  noteName,
} from '../src/params';

describe('R-U1 numeric control logic (UI unit part)', () => {
  const coarse = { min: 0, max: 31 };
  const ol = { min: 0, max: 99 };
  const alg = { min: 0, max: 31, offset: 1 };
  const toggle = { min: 0, max: 1 };

  it('R-U1 typing a number commits it; out of range clamps; non-numbers revert', () => {
    expect(parseEntry('12', coarse)).toBe(12);
    expect(parseEntry('150', coarse)).toBe(31);
    expect(parseEntry('150', ol)).toBe(99);
    expect(parseEntry('150', toggle)).toBe(1);
    expect(parseEntry('-5', ol)).toBe(0);
    expect(parseEntry('abc', ol)).toBeNull();
    expect(parseEntry('', ol)).toBeNull();
    expect(parseEntry('1e3', ol)).toBeNull();
    expect(parseEntry(' 42 ', ol)).toBe(42);
    expect(parseEntry('12.6', ol)).toBe(13);
  });

  it('R-U1 typed numbers use the displayed unit (algorithm 1–32, detune −7…+7)', () => {
    expect(parseEntry('5', alg)).toBe(4);
    expect(parseEntry('150', alg)).toBe(31);
    expect(parseEntry('0', alg)).toBe(0);
    const detune = { min: 0, max: 14, offset: voiceEntryOffset(opParam(1, OP.DETUNE)) };
    expect(parseEntry('+3', detune)).toBe(10);
    expect(parseEntry('-7', detune)).toBe(0);
    expect(parseEntry('−2', detune)).toBe(5);
  });

  it('R-U1 wheel is ±1, Shift+wheel ±10 (up increases)', () => {
    expect(wheelDelta(-100, 0, false)).toBe(1);
    expect(wheelDelta(100, 0, false)).toBe(-1);
    expect(wheelDelta(-3, 0, true)).toBe(10);
    expect(wheelDelta(0, 50, true)).toBe(-10); // Shift+wheel reported horizontally
    expect(wheelDelta(0, 0, false)).toBe(0);
    expect(step(95, 10, ol)).toBe(99);
    expect(step(3, -10, ol)).toBe(0);
  });

  it('R-U1 vertical drag: 40 px = +10, Ctrl/Cmd+drag 40 px = +2–3', () => {
    expect(dragSteps(40, false)).toBe(10);
    expect(dragSteps(-40, false)).toBe(-10);
    const fine = dragSteps(40, true);
    expect(fine).toBeGreaterThanOrEqual(2);
    expect(fine).toBeLessThanOrEqual(3);
    const t = new DragTracker(0, 200, 50, false, ol);
    expect(t.move(0, 198, false)).toBe(50);
    expect(t.dragged).toBe(false);
    expect(t.move(0, 160, false)).toBe(60);
    expect(t.dragged).toBe(true);
    // Switching to fine mid-drag re-bases instead of jumping.
    expect(t.move(0, 160, true)).toBe(60);
    expect(t.move(0, 128, true)).toBe(62);
    expect(t.move(0, -1000, false)).toBe(99);
  });

  it('R-U1 digits, sign and point start typed entry', () => {
    for (const k of ['0', '9', '-', '+', '.']) expect(isEntryStartKey(k)).toBe(true);
    for (const k of ['a', 'Enter', ' ', 'ArrowUp']) expect(isEntryStartKey(k)).toBe(false);
  });
});

describe('F-4.5 programmer name field', () => {
  it('F-4.5 the programmer field turns é😀 into ?? and truncates to 10', () => {
    expect(sanitizeName('é😀')).toBe('??');
    expect(sanitizeName('ABCDEFGHIJKLMN')).toBe('ABCDEFGHIJ');
    expect(sanitizeName('a😀b😀c😀d😀e😀f')).toBe('a?b?c?d?e?');
  });

  it('F-4.5 accepts 0x20–0x7E; RTL, control and other characters become ?', () => {
    expect(sanitizeName(' ~Az09!')).toBe(' ~Az09!');
    expect(sanitizeName('שלום')).toBe('????');
    expect(sanitizeName('a\tb\u007f')).toBe('a?b?');
    expect(sanitizeName('👨‍👩‍👧')).toBe('?');
  });

  it('F-4.5 shorter names are padded with spaces to 10', () => {
    expect(padName('PAD')).toBe('PAD       ');
    expect(nameCodes('AB')).toEqual([65, 66, 32, 32, 32, 32, 32, 32, 32, 32]);
  });
});

describe('Algorithm diagrams (§5.5, R-E8)', () => {
  it('the UI table equals FmCore::algorithms in core/fm/third_party/msfa/fm_core.cc', () => {
    const src = readFileSync(resolve(__dirname, '../../core/fm/third_party/msfa/fm_core.cc'), 'utf8');
    const rows = [...src.matchAll(/\{\s*\{\s*((?:0x[0-9a-f]{2}\s*,\s*){5}0x[0-9a-f]{2})\s*\}\s*\}/gi)].map((m) =>
      m[1].split(',').map((x) => parseInt(x.trim(), 16)),
    );
    expect(rows).toHaveLength(32);
    expect(rows).toEqual(MSFA_ALGORITHMS.map((r) => [...r]));
  });

  it('carriers derived by the diagram rule equal the R-E8 carrier counts', () => {
    const want = [2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 4, 4, 4, 5, 5, 3, 3, 3, 4, 4, 5, 6];
    expect(ALGORITHMS.map((a) => a.carriers.length)).toEqual(want);
    for (const a of ALGORITHMS) for (const c of a.carriers) expect(a.row[c]).toBe(0);
  });

  it('modulators sit above their targets; algorithms 4 and 6 feed back across a chain', () => {
    for (const a of ALGORITHMS) for (const [m, t] of a.edges) expect(a.row[m]).toBeGreaterThan(a.row[t]);
    expect(ALGORITHMS[3].feedback).toEqual({ from: 4, to: 6 });
    expect(ALGORITHMS[5].feedback).toEqual({ from: 5, to: 6 });
    expect(ALGORITHMS[0].feedback).toEqual({ from: 6, to: 6 });
    expect(ALGORITHMS[1].feedback).toEqual({ from: 2, to: 2 });
    expect(ALGORITHMS[31].feedback).toEqual({ from: 6, to: 6 });
    // Algorithm 1: 2→1, 6→5→4→3.
    expect(ALGORITHMS[0].edges).toEqual([[2, 1], [4, 3], [5, 4], [6, 5]]);
  });

  it('chart groups are 1–6, 7–18, 19–25, 26–32', () => {
    expect([1, 6, 7, 18, 19, 25, 26, 32].map(algorithmGroup)).toEqual([0, 0, 1, 1, 2, 2, 3, 3]);
  });
});

describe('LCD font and LED segments (§11)', () => {
  it('font5x7 has a 5×7 glyph for every code 0x20–0x7E', () => {
    for (let c = 0x20; c <= 0x7e; c++) {
      const g = FONT5X7[String.fromCharCode(c)];
      expect(g, `glyph ${c.toString(16)}`).toBeDefined();
      expect(g).toHaveLength(7);
      for (const row of g) expect(row).toMatch(/^[.#]{5}$/);
    }
    expect(Object.keys(FONT5X7)).toHaveLength(95);
    // Every printable glyph except the space lights something; glyphs are distinct.
    const seen = new Set<string>();
    for (let c = 0x21; c <= 0x7e; c++) {
      expect(glyphRows(c).some((r) => r !== 0)).toBe(true);
      const key = FONT5X7[String.fromCharCode(c)].join('');
      expect(seen.has(key), String.fromCharCode(c)).toBe(false);
      seen.add(key);
    }
  });

  it('the block cursor lights all 5×8 dots; codes outside 0x20–0x7E show as a space', () => {
    expect(cellDots(0x41, true)).toHaveLength(40);
    expect(cellDots(0x00)).toEqual([]);
    expect(cellDots(0x7f)).toEqual([]);
    expect(glyphRows(0x41)).toEqual([0b01110, 0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001]);
  });

  it('LED shows 1–32 without a leading zero; 0 is blank', () => {
    expect(ledSegments(0)).toEqual(['', '']);
    expect(ledSegments(7)).toEqual(['', DIGIT_SEGMENTS[7]]);
    expect(ledSegments(10)).toEqual([DIGIT_SEGMENTS[1], DIGIT_SEGMENTS[0]]);
    expect(ledSegments(32)).toEqual([DIGIT_SEGMENTS[3], DIGIT_SEGMENTS[2]]);
    expect(DIGIT_SEGMENTS[8]).toBe('abcdefg');
  });
});

describe('Canvas scaling (§5.1)', () => {
  it('fits uniformly, clamped to 50–200 %', () => {
    expect(fitScale(1280, 1000, 'standalone', 0)).toBe(1);
    expect(fitScale(2560, 1000, 'standalone', 0)).toBe(1);
    expect(fitScale(640, 800, 'web', 0)).toBe(0.5);
    expect(fitScale(300, 200, 'web', 0)).toBe(0.5);
    expect(fitScale(5120, 4000, 'web', 0)).toBe(2);
  });
  it('web: the zoom setting caps the scale; desktop: the UI just fits', () => {
    expect(fitScale(2560, 2000, 'web', 120)).toBe(1.2);
    expect(fitScale(1000, 1000, 'web', 120)).toBeCloseTo(1000 / 1280);
    expect(fitScale(2560, 2000, 'standalone', 120)).toBe(2);
    expect(fitScale(2560, 2000, 'plugin', 50)).toBe(2);
  });
});

describe('Parameter mirror of params.h', () => {
  it('operator blocks are stored OP6 first; ranges match §4.1/§4.2', () => {
    expect(opParam(6, 0)).toBe(0);
    expect(opParam(1, OP.LEVEL)).toBe(105 + 16);
    expect(opOfIndex(0)).toBe(6);
    expect(opOfIndex(125)).toBe(1);
    expect(voiceMax(opParam(3, OP.DETUNE))).toBe(14);
    expect(voiceMax(opParam(3, OP.COARSE))).toBe(31);
    expect(voiceMax(G.ALGORITHM)).toBe(31);
    expect(voiceMax(G.TRANSPOSE)).toBe(48);
    expect(voiceMax(G.LFO_WAVE)).toBe(5);
    expect(functionMax(65)).toBe(12);
    expect(functionMax(71)).toBe(7);
    expect(maskBit(1)).toBe(0b100000);
    expect(maskBit(6)).toBe(0b000001);
    expect(noteName(21)).toBe('A -1');
    expect(noteName(60)).toBe('C 3');
  });

  it('R-U2 Init operator: R-P10 operator values with output level 0', () => {
    expect(INIT_OPERATOR).toHaveLength(21);
    expect(INIT_OPERATOR.slice(0, 8)).toEqual([99, 99, 99, 99, 99, 99, 99, 0]);
    expect(INIT_OPERATOR[OP.LEVEL]).toBe(0);
    expect(INIT_OPERATOR[OP.COARSE]).toBe(1);
    expect(INIT_OPERATOR[OP.DETUNE]).toBe(7);
    expect(INIT_OPERATOR[OP.MODE]).toBe(0);
  });

  it('the TS mirror agrees with core/include/vdx/params.h', () => {
    const h = readFileSync(resolve(__dirname, '../../core/include/vdx/params.h'), 'utf8');
    const m = /kOpMax\[kOpBytes\]\s*=\s*\{([^}]*)\}/.exec(h);
    expect(m).not.toBeNull();
    const opMax = m![1].split(',').map((x) => Number(x.trim()));
    expect(opMax).toEqual(Array.from({ length: 21 }, (_, k) => voiceMax(k)));
  });
});

describe('Bank manager helpers', () => {
  it('long device names get a middle ellipsis (§8)', () => {
    const s = 'An Unusually Long Keyboard Controller Name With Several Words MIDI 2';
    const e = midEllipsis(s, 20);
    expect(e).toHaveLength(20);
    expect(e.startsWith('An Unusua')).toBe(true);
    expect(e.endsWith('s MIDI 2')).toBe(true);
    expect(e).toContain('…');
    expect(midEllipsis('Short', 20)).toBe('Short');
  });

  it('Send bank / Request dump are disabled without MIDI output or SysEx (§5.7)', () => {
    expect(midiOutBlocked({ build: 'web', midi: 'unavailable', output: null })).toBe('MIDI output not available');
    expect(midiOutBlocked({ build: 'web', midi: 'nosysex', output: 'o' })).toBe('SysEx not permitted');
    expect(midiOutBlocked({ build: 'web', midi: 'ok', output: null })).toBe('MIDI output not available');
    expect(midiOutBlocked({ build: 'web', midi: 'ok', output: 'o' })).toBeNull();
    expect(midiOutBlocked({ build: 'plugin', midi: 'ok', output: null })).toBeNull();
  });

  it('files are sent as base64', () => {
    expect(toBase64(new Uint8Array([0xf0, 0x43, 0x00, 0xf7]))).toBe('8EMA9w==');
  });
});

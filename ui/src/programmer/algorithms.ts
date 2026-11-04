// Algorithm routing for the diagrams (SPEC §5.5, R-E8).
//
// MSFA_ALGORITHMS is the `FmCore::algorithms` table from
// core/fm/third_party/msfa/fm_core.cc (Copyright 2012 Google Inc.,
// Apache-2.0; see core/fm/third_party/msfa/NOTICE). Each row lists the
// operator flags in computation order OP6 → OP1. A unit test checks that this
// copy equals the C++ table.

export const MSFA_ALGORITHMS: readonly (readonly number[])[] = [
  [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14], // 1
  [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14], // 2
  [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14], // 3
  [0xc1, 0x11, 0x94, 0x01, 0x11, 0x14], // 4
  [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14], // 5
  [0xc1, 0x94, 0x01, 0x14, 0x01, 0x14], // 6
  [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14], // 7
  [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14], // 8
  [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14], // 9
  [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14], // 10
  [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14], // 11
  [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14], // 12
  [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14], // 13
  [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14], // 14
  [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14], // 15
  [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14], // 16
  [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14], // 17
  [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14], // 18
  [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14], // 19
  [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14], // 20
  [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14], // 21
  [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14], // 22
  [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04], // 23
  [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04], // 24
  [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04], // 25
  [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04], // 26
  [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04], // 27
  [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14], // 28
  [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04], // 29
  [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04], // 30
  [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04], // 31
  [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04], // 32
];

const OUT_BUS_MASK = 0x03;
const OUT_BUS_ADD = 0x04;
const IN_BUS_SHIFT = 4;
const FB_IN = 0x40;
const FB_OUT = 0x80;

export interface AlgorithmGraph {
  /** Algorithm number 1..32. */
  number: number;
  /** Carrier operators (output bus), ascending. */
  carriers: number[];
  /** Modulation edges [modulator, target] (operators 1..6). */
  edges: [number, number][];
  /** Feedback loop: output of `from` feeds back into `to` (from === to for self feedback). */
  feedback: { from: number; to: number };
  /** Row per operator: 0 = carriers (bottom), increasing upwards. Index 1..6. */
  row: number[];
  /** Column (may be fractional) per operator, 0-based from the left. Index 1..6. */
  col: number[];
  rows: number;
  cols: number;
}

/**
 * Derives the routing from the MSFA bus flags by simulating the buses in
 * computation order (OP6 → OP1): an operator writes bus 1/2 (replacing, or
 * adding with OUT_BUS_ADD) or the output (bus 0); its modulators are the
 * operators whose signal is on its input bus when it is computed.
 */
export function deriveAlgorithm(index: number): AlgorithmGraph {
  const flags = MSFA_ALGORITHMS[index];
  const bus: number[][] = [[], [], []];
  const mods: number[][] = [[], [], [], [], [], [], []];
  const targets: number[][] = [[], [], [], [], [], [], []];
  const carriers: number[] = [];
  let fbIn = 0;
  const fbOut: number[] = [];
  for (let j = 0; j < 6; j++) {
    const op = 6 - j;
    const f = flags[j];
    const inBus = (f >> IN_BUS_SHIFT) & 3;
    const outBus = f & OUT_BUS_MASK;
    if (inBus) {
      for (const m of bus[inBus]) {
        mods[op].push(m);
        targets[m].push(op);
      }
    }
    if (outBus === 0) carriers.push(op);
    else if (f & OUT_BUS_ADD) bus[outBus] = [...bus[outBus], op];
    else bus[outBus] = [op];
    if (f & FB_IN) fbIn = op;
    if (f & FB_OUT) fbOut.push(op);
  }
  carriers.sort((a, b) => a - b);
  // Chain feedback (algorithms 4 and 6): the FB_OUT operator other than the FB_IN one.
  const from = fbOut.find((o) => o !== fbIn) ?? fbIn;
  const feedback = { from, to: fbIn };

  const edges: [number, number][] = [];
  for (let op = 1; op <= 6; op++) for (const t of targets[op]) edges.push([op, t]);

  // Rows: carriers 0; a modulator sits one row above the highest of its targets.
  const row = new Array(7).fill(0);
  const rowOf = (op: number): number => {
    if (targets[op].length === 0) return 0;
    return 1 + Math.max(...targets[op].map(rowOf));
  };
  for (let op = 1; op <= 6; op++) row[op] = rowOf(op);

  // Columns: depth-first from the carriers in ascending order. A modulator is
  // placed under its lowest-numbered target; leaves take the next free column
  // and a parent is centred over its first and last child. A modulator with
  // several targets is then centred over all of them.
  const col = new Array(7).fill(0);
  const placed = new Array(7).fill(false);
  let next = 0;
  const primary = (m: number): number => Math.min(...targets[m]);
  const place = (op: number): void => {
    placed[op] = true;
    const kids = mods[op].filter((m) => primary(m) === op && !placed[m]).sort((a, b) => a - b);
    if (kids.length === 0) {
      col[op] = next++;
      return;
    }
    for (const k of kids) place(k);
    col[op] = (col[kids[0]] + col[kids[kids.length - 1]]) / 2;
  };
  for (const c of carriers) place(c);
  for (let op = 1; op <= 6; op++) {
    if (targets[op].length > 1) col[op] = targets[op].reduce((s, t) => s + col[t], 0) / targets[op].length;
  }
  return {
    number: index + 1,
    carriers,
    edges,
    feedback,
    row,
    col,
    rows: Math.max(...row.slice(1)) + 1,
    cols: next,
  };
}

export const ALGORITHMS: readonly AlgorithmGraph[] = MSFA_ALGORITHMS.map((_, i) => deriveAlgorithm(i));

/** Group (0..3) of algorithm 1..32 for the chart tints: 1–6, 7–18, 19–25, 26–32. */
export function algorithmGroup(n: number): number {
  if (n <= 6) return 0;
  if (n <= 18) return 1;
  if (n <= 25) return 2;
  return 3;
}

export function isCarrier(algIndex: number, op: number): boolean {
  return ALGORITHMS[algIndex]?.carriers.includes(op) ?? false;
}

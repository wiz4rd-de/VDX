// 2-digit 7-segment LED (SPEC §5.4, §11). Segments a–g in the usual layout:
//    aaa
//   f   b
//    ggg
//   e   c
//    ddd
export type Segment = 'a' | 'b' | 'c' | 'd' | 'e' | 'f' | 'g';
export const SEGMENT_NAMES: readonly Segment[] = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];

export const DIGIT_SEGMENTS: Readonly<Record<number, string>> = {
  0: 'abcdef',
  1: 'bc',
  2: 'abdeg',
  3: 'abcdg',
  4: 'bcfg',
  5: 'acdfg',
  6: 'acdefg',
  7: 'abc',
  8: 'abcdefg',
  9: 'abcdfg',
};

/**
 * Lit segments for the LED value 0…32 (0 = blank). Tens digit first; the
 * tens digit is blank for 1–9 (no leading zero).
 */
export function ledSegments(v: number): [string, string] {
  if (!Number.isInteger(v) || v <= 0 || v > 99) return ['', ''];
  const tens = Math.floor(v / 10);
  const units = v % 10;
  return [tens === 0 ? '' : DIGIT_SEGMENTS[tens], DIGIT_SEGMENTS[units]];
}

/**
 * Segment polygons for one digit whose box is w × h at (x, y), segment
 * thickness t. Returns SVG `points` strings.
 */
export function segmentPolygons(x: number, y: number, w: number, h: number, t: number): Record<Segment, string> {
  const g = 0.6; // gap between neighbouring segments
  const ht = t / 2;
  const midY = y + h / 2;
  const horiz = (cy: number): string => {
    const x0 = x + ht + g;
    const x1 = x + w - ht - g;
    return pts([
      [x0, cy],
      [x0 + ht, cy - ht],
      [x1 - ht, cy - ht],
      [x1, cy],
      [x1 - ht, cy + ht],
      [x0 + ht, cy + ht],
    ]);
  };
  const vert = (cx: number, y0: number, y1: number): string =>
    pts([
      [cx, y0 + g],
      [cx + ht, y0 + ht + g],
      [cx + ht, y1 - ht - g],
      [cx, y1 - g],
      [cx - ht, y1 - ht - g],
      [cx - ht, y0 + ht + g],
    ]);
  return {
    a: horiz(y + ht),
    g: horiz(midY),
    d: horiz(y + h - ht),
    f: vert(x + ht, y + ht, midY),
    b: vert(x + w - ht, y + ht, midY),
    e: vert(x + ht, midY, y + h - ht),
    c: vert(x + w - ht, midY, y + h - ht),
  };
}

function pts(p: [number, number][]): string {
  return p.map(([px, py]) => `${round(px)},${round(py)}`).join(' ');
}

const round = (n: number): number => Math.round(n * 100) / 100;

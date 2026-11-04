<script lang="ts">
  // 16×2 character LCD (§5.2, §5.3): cells of 5×8 dots, dot pitch 2.4 px,
  // cell pitch 14 × 24 px. The cursor cell alternates every 400 ms between
  // the character and a fully lit 5×8 block; `lit:false` shows every dot in
  // --lcd-off.
  import { cellDots, GLYPH_COLS, CELL_ROWS } from './font5x7';

  interface Props {
    x: number;
    y: number;
    l1: string;
    l2: string;
    row: number;
    col: number;
    cursor: boolean;
    lit: boolean;
    blinkOn: boolean;
  }
  let { x, y, l1, l2, row, col, cursor, lit, blinkOn }: Props = $props();

  const W = 232;
  const H = 74;
  const PITCH = 2.4;
  const DOT = 2.0;
  const CELL_W = 14;
  const CELL_H = 24;
  const GRID_X = (W - 16 * CELL_W) / 2; // 4
  const GRID_Y = (H - 2 * CELL_H) / 2; // 13
  const INSET_X = (CELL_W - GLYPH_COLS * PITCH) / 2; // 1
  const INSET_Y = (CELL_H - CELL_ROWS * PITCH) / 2; // 2.4

  const r = (n: number): number => Math.round(n * 100) / 100;

  function dotsPath(dots: [number, number][], ox: number, oy: number): string {
    let d = '';
    for (const [c, rr] of dots) d += `M${r(ox + c * PITCH)} ${r(oy + rr * PITCH)}h${DOT}v${DOT}h-${DOT}z`;
    return d;
  }

  // Every dot of the display, drawn in --lcd-off.
  const allDots = (() => {
    const block = cellDots(0x20, true);
    let d = '';
    for (let rr = 0; rr < 2; rr++)
      for (let c = 0; c < 16; c++) d += dotsPath(block, GRID_X + c * CELL_W + INSET_X, GRID_Y + rr * CELL_H + INSET_Y);
    return d;
  })();

  const cache = new Map<string, string>();
  function cellPath(code: number, block: boolean, c: number, rr: number): string {
    const key = `${code}:${block ? 1 : 0}:${c}:${rr}`;
    let p = cache.get(key);
    if (p === undefined) {
      p = dotsPath(cellDots(code, block), GRID_X + c * CELL_W + INSET_X, GRID_Y + rr * CELL_H + INSET_Y);
      cache.set(key, p);
    }
    return p;
  }

  const ink = $derived.by(() => {
    if (!lit) return '';
    let d = '';
    const lines = [l1, l2];
    for (let rr = 0; rr < 2; rr++) {
      for (let c = 0; c < 16; c++) {
        const block = cursor && blinkOn && row === rr && col === c;
        const code = lines[rr].charCodeAt(c) || 0x20;
        if (code === 0x20 && !block) continue;
        d += cellPath(code, block, c, rr);
      }
    }
    return d;
  });

</script>

<div
  class="lcd"
  style="left:{x}px;top:{y}px;width:{W}px;height:{H}px"
  role="img"
  aria-label="LCD"
  data-testid="lcd"
  data-l1={lit ? l1 : ''}
  data-l2={lit ? l2 : ''}
  data-lit={lit ? '1' : '0'}
  data-cursor={cursor ? `${row},${col}` : ''}
>
  <svg width={W} height={H} viewBox="0 0 {W} {H}" aria-hidden="true">
    <rect x="0" y="0" width={W} height={H} rx="4" fill="var(--body)" />
    <rect x="2" y="8" width={W - 4} height={H - 16} rx="1" fill="var(--lcd-bg)" />
    <path d={allDots} fill="var(--lcd-off)" />
    {#if ink}<path d={ink} fill="var(--lcd-ink)" />{/if}
  </svg>
</div>

<style>
  .lcd {
    position: absolute;
  }
  svg {
    display: block;
  }
</style>

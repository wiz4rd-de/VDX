<script lang="ts">
  // Envelope graph with four draggable rate/level handles (§5.5 pitch EG,
  // §5.6 operator EG). Horizontal drag changes the stage rate (right = slower),
  // vertical drag its level. Axis mapping is §14 latitude: each stage is
  // 4 + 30 × (99 − rate) / 99 px wide, the sustain segment a fixed width.
  import { store } from '../store.svelte';
  import { sendSoon, flush } from '../bridge';
  import { beginDrag, clamp99 } from './graphDrag';

  interface Props {
    /** Index of rate 1; rates are r0..r0+3, levels r0+4..r0+7. */
    r0: number;
    width: number;
    height: number;
    /** Pitch EG: draw the 50 (no shift) centre line. */
    centre?: boolean;
    testid: string;
  }
  let { r0, width, height, centre = false, testid }: Props = $props();

  const PAD_X = 4;
  const PAD_Y = 6;
  const SUSTAIN = 22;
  const stageW = $derived((width - 2 * PAD_X - SUSTAIN) / 4);
  const rateW = (r: number): number => 4 + (stageW - 4) * ((99 - r) / 99);
  const yOf = (l: number): number => PAD_Y + ((99 - l) / 99) * (height - 2 * PAD_Y);

  const pts = $derived.by(() => {
    const R = [0, 1, 2, 3].map((k) => store.params[r0 + k]);
    const L = [0, 1, 2, 3].map((k) => store.params[r0 + 4 + k]);
    const p0 = { x: PAD_X, y: yOf(L[3]) };
    const p1 = { x: p0.x + rateW(R[0]), y: yOf(L[0]) };
    const p2 = { x: p1.x + rateW(R[1]), y: yOf(L[1]) };
    const p3 = { x: p2.x + rateW(R[2]), y: yOf(L[2]) };
    const ps = { x: p3.x + SUSTAIN, y: p3.y };
    const p4 = { x: ps.x + rateW(R[3]), y: yOf(L[3]) };
    return { p0, handles: [p1, p2, p3, p4], ps };
  });

  const line = $derived.by(() => {
    const { p0, handles: [p1, p2, p3, p4], ps } = pts;
    return `M${p0.x} ${p0.y}L${p1.x} ${p1.y}L${p2.x} ${p2.y}L${p3.x} ${p3.y}L${ps.x} ${ps.y}L${p4.x} ${p4.y}`;
  });
  const area = $derived(`${line}V${height - PAD_Y}H${PAD_X}Z`);

  function grab(ev: PointerEvent, k: number): void {
    let r = 0;
    let l = 0;
    beginDrag(ev, {
      start() {
        r = store.params[r0 + k];
        l = store.params[r0 + 4 + k];
      },
      move(dx, dy) {
        const nr = clamp99(r - (dx * 99) / (stageW - 4));
        const nl = clamp99(l - (dy * 99) / (height - 2 * PAD_Y));
        if (nr !== store.params[r0 + k]) sendSoon({ t: 'setParam', i: r0 + k, v: nr });
        if (nl !== store.params[r0 + 4 + k]) sendSoon({ t: 'setParam', i: r0 + 4 + k, v: nl });
      },
      end: flush,
    });
  }
</script>

<svg {width} {height} viewBox="0 0 {width} {height}" data-testid={testid} aria-hidden="true">
  <rect x="0" y="0" {width} {height} rx="2" fill="var(--body)" />
  {#if centre}
    <line x1={PAD_X} x2={width - PAD_X} y1={yOf(50)} y2={yOf(50)} stroke="var(--panel)" stroke-dasharray="2 2" />
  {/if}
  {#if store.ready}
    <path d={area} fill="var(--panel)" opacity="0.7" />
    <line x1={pts.ps.x} x2={pts.ps.x} y1={PAD_Y - 2} y2={height - PAD_Y + 2} stroke="var(--brown-legend)" stroke-dasharray="2 2" />
    <path d={line} fill="none" stroke="var(--legend)" stroke-width="1.5" stroke-linejoin="round" />
    {#each pts.handles as p, k}
      <!-- Pointer-only shortcut for the R/L fields below (which carry focus and ARIA names). -->
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <circle
        class="handle"
        cx={p.x}
        cy={p.y}
        r="4"
        data-testid="{testid}-h{k + 1}"
        onpointerdown={(ev) => grab(ev, k)}
      />
      <text x={p.x} y={p.y < 16 ? p.y + 13 : p.y - 6} text-anchor="middle">{k + 1}</text>
    {/each}
  {/if}
</svg>

<style>
  svg {
    display: block;
  }
  .handle {
    fill: var(--programmer);
    stroke: var(--legend);
    stroke-width: 1.5;
    cursor: move;
    touch-action: none;
  }
  .handle:hover {
    fill: var(--legend);
  }
  text {
    font: 600 8px 'Barlow Condensed', sans-serif;
    fill: var(--brown-legend);
    pointer-events: none;
  }
</style>

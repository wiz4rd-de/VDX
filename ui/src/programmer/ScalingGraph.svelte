<script lang="ts">
  // Keyboard level-scaling graph (§5.6): break point (draggable) and the left
  // and right curves. Keys MIDI 21…120 run left to right (break point 0…99).
  // Curve shapes are drawn schematically (§14 latitude): LIN rises linearly,
  // EXP exponentially, − curves attenuate (down), + curves boost (up).
  import { store } from '../store.svelte';
  import { sendSoon, flush } from '../bridge';
  import { beginDrag, clamp99 } from './graphDrag';
  import { OP, opParam } from '../params';

  interface Props {
    op: number;
    width: number;
    height: number;
  }
  let { op, width, height }: Props = $props();

  const PAD = 4;
  const xOfBp = (bp: number): number => PAD + (bp / 99) * (width - 2 * PAD);
  const mid = $derived(height / 2);

  function shape(curve: number, semis: number): number {
    const lin = curve === 0 || curve === 3;
    const v = lin ? semis / 48 : (Math.pow(2, semis / 12) - 1) / 15;
    return Math.min(1, v);
  }

  const paths = $derived.by(() => {
    const bp = store.params[opParam(op, OP.BREAK_POINT)];
    const ld = store.params[opParam(op, OP.LEFT_DEPTH)];
    const rd = store.params[opParam(op, OP.RIGHT_DEPTH)];
    const lc = store.params[opParam(op, OP.LEFT_CURVE)];
    const rc = store.params[opParam(op, OP.RIGHT_CURVE)];
    const xb = xOfBp(bp);
    const amp = height / 2 - 5;
    const y = (curve: number, depth: number, semis: number): number => {
      const sign = curve >= 2 ? 1 : -1;
      return mid - sign * (depth / 99) * shape(curve, semis) * amp;
    };
    let left = `M${xb} ${mid}`;
    for (let x = xb; x >= PAD; x -= 2) {
      const semis = ((xb - x) / (width - 2 * PAD)) * 99;
      left += `L${x} ${y(lc, ld, semis)}`;
    }
    let right = `M${xb} ${mid}`;
    for (let x = xb; x <= width - PAD; x += 2) {
      const semis = ((x - xb) / (width - 2 * PAD)) * 99;
      right += `L${x} ${y(rc, rd, semis)}`;
    }
    return { xb, left, right };
  });

  function grab(ev: PointerEvent): void {
    const i = opParam(op, OP.BREAK_POINT);
    let bp0 = 0;
    beginDrag(ev, {
      start() {
        bp0 = store.params[i];
      },
      move(dx) {
        const v = clamp99(bp0 + (dx * 99) / (width - 2 * PAD));
        if (v !== store.params[i]) sendSoon({ t: 'setParam', i, v });
      },
      end: flush,
    });
  }
</script>

<svg {width} {height} viewBox="0 0 {width} {height}" data-testid="ls-graph-{op}" aria-hidden="true">
  <rect x="0" y="0" {width} {height} rx="2" fill="var(--body)" />
  <line x1={PAD} x2={width - PAD} y1={mid} y2={mid} stroke="var(--panel)" />
  {#if store.ready}
    <path d={paths.left} fill="none" stroke="var(--legend)" stroke-width="1.5" />
    <path d={paths.right} fill="none" stroke="var(--legend)" stroke-width="1.5" />
    <line x1={paths.xb} x2={paths.xb} y1="3" y2={height - 3} stroke="var(--brown-legend)" stroke-width="1" />
    <!-- Pointer-only shortcut for the BREAK PT field (which carries focus and the ARIA name). -->
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <rect
      class="handle"
      x={paths.xb - 4}
      y="2"
      width="8"
      height="8"
      rx="1"
      data-testid="ls-graph-{op}-bp"
      onpointerdown={grab}
    />
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
    cursor: ew-resize;
    touch-action: none;
  }
  .handle:hover {
    fill: var(--legend);
  }
</style>

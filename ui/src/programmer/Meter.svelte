<script lang="ts">
  // Operator output meter (§5.6): −60…0 dBFS from `meter` events. The bar
  // rises immediately and falls back at 60 dB per 300 ms (peak fall).
  import { untrack } from 'svelte';
  import { store } from '../store.svelte';

  interface Props {
    op: number;
    width: number;
    height: number;
  }
  let { op, width, height }: Props = $props();

  const FALL_DB_PER_MS = 60 / 300;
  let shown = $state(-60);
  let last = 0;
  let raf = 0;

  function tickFrame(now: number): void {
    const target = store.meter[op - 1];
    const dt = last ? now - last : 16;
    last = now;
    const fallen = shown - FALL_DB_PER_MS * dt;
    shown = Math.max(target, fallen, -60);
    if (shown > -60 || target > -60) raf = requestAnimationFrame(tickFrame);
    else {
      raf = 0;
      last = 0;
    }
  }

  $effect(() => {
    void store.meterSeq;
    const target = store.meter[op - 1];
    untrack(() => {
      if (target > shown) shown = target;
      if (!raf && typeof requestAnimationFrame === 'function' && shown > -60) {
        last = 0;
        raf = requestAnimationFrame(tickFrame);
      }
    });
  });

  $effect(() => () => {
    if (raf) cancelAnimationFrame(raf);
  });

  const w = $derived(((shown + 60) / 60) * width);
</script>

<svg
  {width}
  {height}
  viewBox="0 0 {width} {height}"
  role="meter"
  aria-label="OP{op} output level"
  aria-valuemin={-60}
  aria-valuemax={0}
  aria-valuenow={Math.round(shown)}
  data-testid="meter-{op}"
>
  <rect x="0" y="0" {width} {height} rx="1" fill="var(--body)" />
  <rect x="0" y="0" width={w} {height} rx="1" fill="var(--dx-green)" />
  {#each [-48, -36, -24, -12] as db}
    <line x1={((db + 60) / 60) * width} x2={((db + 60) / 60) * width} y1="0" y2={height} stroke="var(--programmer)" />
  {/each}
</svg>

<style>
  svg {
    display: block;
  }
</style>

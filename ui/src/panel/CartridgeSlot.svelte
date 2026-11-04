<script lang="ts">
  // Cartridge slot (§5.2): the name rotated 90° (reads bottom to top), with an
  // ellipsis after 18 characters, or NO CARTRIDGE. Full name in the tooltip;
  // a click opens the bank manager.
  import { store } from '../store.svelte';

  interface Props {
    x: number;
    y: number;
    w: number;
    h: number;
  }
  let { x, y, w, h }: Props = $props();

  const present = $derived(store.ready && store.cartridge.present);
  const full = $derived(present ? store.cartridge.name : 'NO CARTRIDGE');
  const shown = $derived(full.length > 18 ? `${full.slice(0, 18)}…` : full);
</script>

<button tabindex="0"
  type="button"
  class="slot"
  style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"
  title={full}
  aria-label="Cartridge: {full}"
  data-testid="cartridge-slot"
  onclick={() => (store.overlay = 'bank')}
>
  <svg width={w} height={h} viewBox="0 0 {w} {h}" aria-hidden="true">
    <rect x="0" y="0" width={w} height={h} rx="3" fill="var(--body)" />
    <rect x="6" y="6" width={w - 12} height={h - 12} rx="2" fill={present ? 'var(--programmer)' : 'var(--body)'} stroke="var(--panel)" />
    {#if present}
      <rect x="14" y="14" width={w - 28} height={h - 28} rx="1" fill="var(--overlay)" stroke="var(--brown-legend)" stroke-width="0.75" />
    {/if}
    <text
      x={w / 2}
      y={h / 2}
      transform="rotate(-90 {w / 2} {h / 2})"
      text-anchor="middle"
      dominant-baseline="central"
      class:dim={!present}>{shown}</text
    >
  </svg>
</button>

<style>
  .slot {
    position: absolute;
    margin: 0;
    padding: 0;
    border: none;
    background: none;
    cursor: pointer;
  }
  svg {
    display: block;
  }
  text {
    font: 600 12px 'Barlow Condensed', sans-serif;
    letter-spacing: 0.06em;
    fill: var(--legend);
  }
  text.dim {
    fill: var(--brown-legend);
  }
</style>

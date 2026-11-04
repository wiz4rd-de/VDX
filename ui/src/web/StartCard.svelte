<script lang="ts">
  // §5.9: "Click or press any key to start VDX". The card covers the whole
  // window so the dismissing click never reaches a control; keys are
  // swallowed by the capture-phase guard in input.ts.
  import { dismissStartCard } from './webstate.svelte';

  function swallow(ev: Event): void {
    ev.preventDefault();
    ev.stopPropagation();
  }
</script>

<div
  class="start"
  role="button"
  tabindex="-1"
  aria-label="Click or press any key to start VDX"
  data-testid="start-card"
  onpointerdown={swallow}
  onpointerup={swallow}
  onmousedown={swallow}
  onmouseup={swallow}
  oncontextmenu={swallow}
  onclick={(ev) => {
    swallow(ev);
    dismissStartCard();
  }}
  onkeydown={swallow}
>
  <div class="card">Click or press any key to start VDX</div>
</div>

<style>
  .start {
    position: fixed;
    inset: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: center;
    background: color-mix(in srgb, var(--body) 45%, transparent);
    cursor: pointer;
  }
  .card {
    padding: 22px 36px;
    background: var(--overlay);
    color: var(--legend);
    border: 1px solid var(--legend);
    border-radius: 4px;
    font: 600 20px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.06em;
  }
</style>

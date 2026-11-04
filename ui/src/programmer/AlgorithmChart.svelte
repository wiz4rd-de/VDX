<script lang="ts">
  // Algorithm chart (§5.5): 8 × 4 thumbnails (48 × 58) in reading order, four
  // group tints, the current algorithm outlined, click selects.
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { G } from '../params';
  import { algorithmGroup } from './algorithms';
  import AlgoDiagram from './AlgoDiagram.svelte';

  const X = 16;
  const Y = 270;
  const TW = 48;
  const TH = 58;
  const GAP = 2;
  const current = $derived(store.ready ? store.params[G.ALGORITHM] : -1);
</script>

<div class="block chart" style="left:{X}px;top:{Y}px;width:400px;height:250px" role="group" aria-label="Algorithm chart">
  {#each Array.from({ length: 32 }, (_, i) => i) as i}
    <button tabindex="0"
      type="button"
      class="thumb g{algorithmGroup(i + 1)}"
      class:current={i === current}
      style="left:{(i % 8) * (TW + GAP)}px;top:{5 + Math.floor(i / 8) * (TH + GAP)}px;width:{TW}px;height:{TH}px"
      aria-label="Algorithm {i + 1}"
      aria-pressed={i === current}
      title="Algorithm {i + 1}"
      data-testid="algo-thumb-{i + 1}"
      onclick={() => send({ t: 'setParam', i: G.ALGORITHM, v: i })}
    >
      <span class="n">{i + 1}</span>
      <span class="d"><AlgoDiagram index={i} width={44} height={42} /></span>
    </button>
  {/each}
</div>

<style>
  .chart {
    position: absolute;
  }
  .thumb {
    position: absolute;
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    border: 1px solid transparent;
    border-radius: 2px;
    cursor: pointer;
  }
  .g0 {
    background: color-mix(in srgb, var(--programmer) 78%, var(--violet));
  }
  .g1 {
    background: color-mix(in srgb, var(--programmer) 78%, var(--dx-green));
  }
  .g2 {
    background: color-mix(in srgb, var(--programmer) 78%, var(--brown-legend));
  }
  .g3 {
    background: color-mix(in srgb, var(--programmer) 78%, var(--modulator));
  }
  .thumb.current {
    border: 2px solid var(--legend);
  }
  .n {
    position: absolute;
    left: 3px;
    top: 2px;
    font: 600 10px/1 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    color: var(--legend);
  }
  .d {
    position: absolute;
    left: 2px;
    top: 13px;
  }
</style>

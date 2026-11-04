<script lang="ts">
  // Programmer (§5.5, y 270–1000). Blocks in Tab order: top row left to
  // right, then the six operators (ordered per the Operator order setting),
  // then the function strip.
  import { store } from '../store.svelte';
  import AlgorithmChart from './AlgorithmChart.svelte';
  import AlgorithmDetail from './AlgorithmDetail.svelte';
  import VoiceGlobals from './VoiceGlobals.svelte';
  import Lfo from './Lfo.svelte';
  import PitchEg from './PitchEg.svelte';
  import OperatorPanel from './OperatorPanel.svelte';
  import FunctionStrip from './FunctionStrip.svelte';

  const order = $derived(store.settings.opOrder === 1 ? [6, 5, 4, 3, 2, 1] : [1, 2, 3, 4, 5, 6]);
</script>

<div class="programmer" data-testid="programmer">
  <AlgorithmChart />
  <AlgorithmDetail />
  <VoiceGlobals />
  <Lfo />
  <PitchEg />
  {#each order as op, i (op)}
    <OperatorPanel {op} x={16 + i * 208} y={532} />
  {/each}
  <FunctionStrip />
</div>

<style>
  .programmer {
    position: absolute;
    left: 0;
    top: 0;
    width: 1280px;
    height: 1000px;
    pointer-events: none;
  }
  .programmer > :global(*) {
    pointer-events: auto;
  }
</style>

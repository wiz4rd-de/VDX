<script lang="ts">
  // Algorithm detail (§5.5): large diagram of the current algorithm and the
  // numeric field 1–32.
  import { store } from '../store.svelte';
  import { G } from '../params';
  import AlgoDiagram from './AlgoDiagram.svelte';
  import Param from '../controls/Param.svelte';
</script>

<div class="block" style="left:428px;top:270px;width:200px;height:250px" role="group" aria-label="Algorithm">
  <div class="head">
    <span class="title">ALGORITHM</span>
    <Param voice={G.ALGORITHM} style="width:44px" />
  </div>
  <div class="diagram">
    {#if store.ready}
      <AlgoDiagram index={store.params[G.ALGORITHM]} width={184} height={196} labels />
    {/if}
  </div>
</div>

<style>
  .diagram {
    position: absolute;
    left: 8px;
    top: 44px;
    width: 184px;
    height: 196px;
  }
</style>

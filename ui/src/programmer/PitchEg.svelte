<script lang="ts">
  // Pitch EG (§5.5): graph with 4 draggable points plus 8 numeric fields.
  import { G } from '../params';
  import Param from '../controls/Param.svelte';
  import EnvGraph from './EnvGraph.svelte';
</script>

<div class="block" style="left:1080px;top:270px;width:184px;height:250px" role="group" aria-label="Pitch EG">
  <div class="head"><span class="title">PITCH EG</span></div>
  <div class="graph"><EnvGraph r0={G.PEG_R1} width={168} height={118} centre testid="peg-graph" /></div>
  <div class="fields" style="top:166px">
    {#each [0, 1, 2, 3] as k}
      <div class="f"><span class="lbl">R{k + 1}</span><Param voice={G.PEG_R1 + k} /></div>
    {/each}
  </div>
  <div class="fields" style="top:206px">
    {#each [0, 1, 2, 3] as k}
      <div class="f"><span class="lbl">L{k + 1}</span><Param voice={G.PEG_L1 + k} /></div>
    {/each}
  </div>
</div>

<style>
  .graph {
    position: absolute;
    left: 8px;
    top: 38px;
  }
  .fields {
    position: absolute;
    left: 8px;
    right: 8px;
    display: flex;
    justify-content: space-between;
  }
  .f {
    width: 38px;
  }
</style>

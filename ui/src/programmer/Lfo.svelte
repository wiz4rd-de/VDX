<script lang="ts">
  // LFO (§5.5): wave (6 icon buttons), speed (number plus Hz from `lfoHz`),
  // delay, PMD, AMD, key sync.
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { G } from '../params';
  import Param from '../controls/Param.svelte';

  const WAVES = ['TRI', 'SAW DN', 'SAW UP', 'SQU', 'SIN', 'S/H'];
  // Icon paths in a 21 × 12 box.
  const ICONS = [
    'M1 11L5.5 1L10.5 11L15.5 1L20 11',
    'M1 1L10 11V1L19 11',
    'M1 11L10 1V11L19 1',
    'M1 11V1H10.5V11H20V1',
    'M1 6C3 -1.5 8 -1.5 10.5 6S18 13.5 20 6',
    'M1 8H4.5V2H8V10H11.5V4H15V9H18.5V3H20',
  ];
  const wave = $derived(store.ready ? store.params[G.LFO_WAVE] : -1);
</script>

<div class="block" style="left:872px;top:270px;width:196px;height:250px" role="group" aria-label="LFO">
  <div class="head">
    <span class="title">LFO</span>
    <span class="wave-name" data-testid="lfo-wave-name">{store.ready ? store.paramStr[G.LFO_WAVE].trim() : ''}</span>
  </div>
  <div class="waves" role="radiogroup" aria-label="LFO Wave">
    {#each WAVES as w, k}
      <button tabindex="0"
        type="button"
        class="wave"
        class:on={wave === k}
        role="radio"
        aria-checked={wave === k}
        aria-label={w}
        title="LFO Wave: {w}"
        data-testid="lfo-wave-{k}"
        disabled={!store.ready}
        onclick={() => send({ t: 'setParam', i: G.LFO_WAVE, v: k })}
      >
        <svg width="21" height="12" viewBox="0 0 21 12" aria-hidden="true">
          <path d={ICONS[k]} fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round" />
        </svg>
      </button>
    {/each}
  </div>
  <div class="rows">
    <div class="r">
      <span class="lbl">SPEED</span>
      <span class="hz" data-testid="lfo-hz">{store.ready ? store.lfoHz : ''}</span>
      <div class="c"><Param voice={G.LFO_SPEED} /></div>
    </div>
    <div class="r"><span class="lbl">DELAY</span><div class="c"><Param voice={G.LFO_DELAY} /></div></div>
    <div class="r"><span class="lbl">PM DEPTH</span><div class="c"><Param voice={G.LFO_PMD} /></div></div>
    <div class="r"><span class="lbl">AM DEPTH</span><div class="c"><Param voice={G.LFO_AMD} /></div></div>
    <div class="r"><span class="lbl">KEY SYNC</span><div class="c"><Param voice={G.LFO_KEY_SYNC} kind="toggle" /></div></div>
  </div>
</div>

<style>
  .wave-name {
    font: 600 11px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.06em;
    color: var(--legend);
  }
  .waves {
    position: absolute;
    left: 8px;
    right: 8px;
    top: 38px;
    display: flex;
    justify-content: space-between;
  }
  .wave {
    box-sizing: border-box;
    width: 28px;
    height: 24px;
    margin: 0;
    padding: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--panel);
    border-radius: 2px;
    background: var(--body);
    color: var(--legend);
    cursor: pointer;
  }
  .wave.on {
    background: var(--dx-green);
    border-color: var(--dx-green);
    color: var(--body);
  }
  .rows {
    position: absolute;
    left: 10px;
    right: 10px;
    top: 76px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  .r {
    display: flex;
    align-items: center;
    height: 22px;
    gap: 6px;
  }
  .r .lbl {
    flex: 1 1 auto;
  }
  .hz {
    font: 600 11px/1 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    color: var(--legend);
    white-space: nowrap;
  }
  .c {
    width: 52px;
  }
</style>

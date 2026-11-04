<script lang="ts">
  // Function strip (§5.5): poly/mono, bend range/step, porta mode/gliss/time,
  // master tune (cents), master volume, then the 4 controller columns
  // (range + PITCH / AMP / EG B toggles).
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { FN, CTL_SOURCES, ASSIGN_BITS } from '../params';
  import Param from '../controls/Param.svelte';
  import ParamControl from '../controls/ParamControl.svelte';

  const SINGLE: { label: string; func?: number; global?: 'masterTune' | 'volume'; w: number; kind?: 'cycle' | 'toggle' }[] = [
    { label: 'POLY/MONO', func: FN.MONO, w: 58, kind: 'cycle' },
    { label: 'BEND RANGE', func: FN.BEND_RANGE, w: 48 },
    { label: 'BEND STEP', func: FN.BEND_STEP, w: 48 },
    { label: 'PORTA MODE', func: FN.PORTA_MODE, w: 80, kind: 'cycle' },
    { label: 'GLISSANDO', func: FN.GLISSANDO, w: 52, kind: 'toggle' },
    { label: 'PORTA TIME', func: FN.PORTA_TIME, w: 48 },
    { label: 'MASTER TUNE', global: 'masterTune', w: 84 },
    { label: 'VOLUME', global: 'volume', w: 48 },
  ];
  const GROUPS = [
    { name: 'WHEEL', range: FN.WHEEL_RANGE, assign: FN.WHEEL_ASSIGN },
    { name: 'FOOT', range: FN.FOOT_RANGE, assign: FN.FOOT_ASSIGN },
    { name: 'BREATH', range: FN.BREATH_RANGE, assign: FN.BREATH_ASSIGN },
    { name: 'AFTER', range: FN.AT_RANGE, assign: FN.AT_ASSIGN },
  ];
  const BIT_LABELS = ['PITCH', 'AMP', 'EG B'];

  function setBit(assign: number, bit: number, on: number): void {
    const cur = store.func[assign];
    send({ t: 'setFunc', i: assign, v: on ? cur | (1 << bit) : cur & ~(1 << bit) });
  }
</script>

<div class="block strip" style="left:16px;top:912px;width:1248px;height:76px" role="group" aria-label="Function">
  <div class="head"><span class="title">FUNCTION</span></div>
  <div class="items">
    {#each SINGLE as s}
      <div class="f" style="width:{s.w}px">
        <span class="lbl">{s.label}</span>
        {#if s.func !== undefined}
          <Param func={s.func} kind={s.kind ?? 'number'} />
        {:else}
          <Param global={s.global} />
        {/if}
      </div>
    {/each}
    {#each GROUPS as g, gi}
      <div class="grp" role="group" aria-label={CTL_SOURCES[gi]}>
        <div class="f" style="width:40px">
          <span class="lbl over">{g.name} RANGE</span>
          <Param func={g.range} />
        </div>
        {#each BIT_LABELS as bl, bit}
          <div class="f" style="width:40px">
            <span class="lbl">&nbsp;</span>
            <ParamControl
              kind="toggle"
              value={(store.func[g.assign] >> bit) & 1}
              display={bl}
              max={1}
              label="{CTL_SOURCES[gi]} {ASSIGN_BITS[bit]}"
              tip="{CTL_SOURCES[gi]} {ASSIGN_BITS[bit]} (OFF, ON)"
              ready={store.ready}
              testid="func-{g.assign}-bit{bit}"
              onchange={(v) => setBit(g.assign, bit, v)}
            />
          </div>
        {/each}
      </div>
    {/each}
  </div>
</div>

<style>
  .strip .head {
    height: 14px;
    top: 4px;
  }
  .items {
    position: absolute;
    left: 10px;
    right: 10px;
    top: 24px;
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
  }
  .over {
    overflow: visible;
  }
  .grp {
    display: flex;
    gap: 5px;
    padding-left: 10px;
    border-left: 1px solid var(--panel);
  }
</style>

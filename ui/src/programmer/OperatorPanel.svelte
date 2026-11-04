<script lang="ts">
  // Operator panel, 200 × 368 (§5.6), top to bottom: header (OP n, on/off
  // switch, carrier/modulator badge), frequency row, output level, KVS / AMS /
  // rate scaling, EG graph + 8 fields, level-scaling graph + curves / depths /
  // break point, output meter. Right-click on the header opens the R-U2 menu.
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { menus } from '../ui.svelte';
  import Param from '../controls/Param.svelte';
  import ParamControl from '../controls/ParamControl.svelte';
  import EnvGraph from './EnvGraph.svelte';
  import ScalingGraph from './ScalingGraph.svelte';
  import Meter from './Meter.svelte';
  import { OP, G, opParam, maskBit } from '../params';
  import { isCarrier } from './algorithms';
  import { toCanvas } from './canvasPoint';

  interface Props {
    op: number;
    x: number;
    y: number;
  }
  let { op, x, y }: Props = $props();

  const p = (offset: number): number => opParam(op, offset);
  const on = $derived(((store.params[G.OP_MASK] >> (6 - op)) & 1) === 1);
  const carrier = $derived(isCarrier(store.params[G.ALGORITHM], op));

  function toggleOp(v: number): void {
    const mask = store.params[G.OP_MASK];
    send({ t: 'setParam', i: G.OP_MASK, v: v ? mask | maskBit(op) : mask & ~maskBit(op) });
  }

  function openMenu(ev: MouseEvent): void {
    ev.preventDefault();
    const pt =
      ev.clientX === 0 && ev.clientY === 0
        ? { x: x + 8, y: y + 28 }
        : toCanvas(ev.clientX, ev.clientY);
    menus.voice = false;
    menus.op = { op, x: pt.x, y: pt.y };
  }

  const EG_LABELS = ['R1', 'R2', 'R3', 'R4', 'L1', 'L2', 'L3', 'L4'];
</script>

<div
  class="block op"
  style="left:{x}px;top:{y}px;width:200px;height:368px"
  role="group"
  aria-label="Operator {op}"
  tabindex="-1"
  data-op-panel={op}
  data-testid="op-panel-{op}"
>
  <div class="hdr" oncontextmenu={openMenu} role="presentation" data-testid="op-header-{op}">
    <span class="title">OP {op}</span>
    {#if store.ready}
      <span class="badge" class:carrier data-testid="op-badge-{op}">{carrier ? 'CARRIER' : 'MODULATOR'}</span>
    {/if}
    <ParamControl
      kind="toggle"
      value={on ? 1 : 0}
      display={on ? 'ON' : 'OFF'}
      max={1}
      label="OP{op} On/Off"
      tip="OP{op} On/Off (OFF, ON)"
      ready={store.ready}
      testid="op-switch-{op}"
      style="width:38px"
      onchange={toggleOp}
    />
  </div>

  <!-- 2. Frequency row (two lines: the mode string is 16 characters, R-P8) -->
  <div class="row" style="top:27px">
    <div class="f" style="width:126px"><span class="lbl">MODE</span><Param voice={p(OP.MODE)} kind="cycle" class="sm" /></div>
    <div class="f" style="width:50px"><span class="lbl">DETUNE</span><Param voice={p(OP.DETUNE)} class="sm" /></div>
  </div>
  <div class="row" style="top:57px">
    <div class="f" style="width:62px"><span class="lbl">COARSE</span><Param voice={p(OP.COARSE)} class="sm" /></div>
    <div class="f" style="width:62px"><span class="lbl">FINE</span><Param voice={p(OP.FINE)} class="sm" /></div>
    <div class="f" style="width:50px">
      <span class="lbl">FREQ</span>
      <span class="freq" data-testid="op-freq-{op}" title="OP{op} frequency">{store.ready ? store.opFreq[op] : ''}</span>
    </div>
  </div>

  <!-- 3. Output level (large) -->
  <div class="row" style="top:88px">
    <div class="f wide" style="width:184px">
      <span class="lbl">OUTPUT LEVEL</span>
      <Param voice={p(OP.LEVEL)} class="big" />
    </div>
  </div>

  <!-- 4. Velocity sensitivity, AMS, rate scaling -->
  <div class="row" style="top:115px">
    <div class="f" style="width:58px"><span class="lbl">KEY VEL</span><Param voice={p(OP.KVS)} class="sm" /></div>
    <div class="f" style="width:58px"><span class="lbl">AMS</span><Param voice={p(OP.AMS)} class="sm" /></div>
    <div class="f" style="width:58px"><span class="lbl">RATE SCL</span><Param voice={p(OP.RATE_SCALING)} class="sm" /></div>
  </div>

  <!-- 5. EG graph + 8 fields -->
  <div class="graph" style="top:146px">
    <EnvGraph r0={p(OP.R1)} width={184} height={90} testid="eg-graph-{op}" />
  </div>
  <div class="row tight" style="top:238px">
    {#each EG_LABELS as l, k}
      <div class="f" style="width:22px"><span class="lbl">{l}</span><Param voice={p(OP.R1 + k)} class="sm" /></div>
    {/each}
  </div>

  <!-- 6. Level-scaling graph, curves, depths, break point -->
  <div class="graph" style="top:269px">
    <ScalingGraph {op} width={184} height={60} />
  </div>
  <div class="row" style="top:331px">
    <div class="f" style="width:34px"><span class="lbl">L CURVE</span><Param voice={p(OP.LEFT_CURVE)} kind="cycle" class="sm" /></div>
    <div class="f" style="width:30px"><span class="lbl">L DEPTH</span><Param voice={p(OP.LEFT_DEPTH)} class="sm" /></div>
    <div class="f" style="width:40px"><span class="lbl">BREAK PT</span><Param voice={p(OP.BREAK_POINT)} class="sm" /></div>
    <div class="f" style="width:30px"><span class="lbl">R DEPTH</span><Param voice={p(OP.RIGHT_DEPTH)} class="sm" /></div>
    <div class="f" style="width:34px"><span class="lbl">R CURVE</span><Param voice={p(OP.RIGHT_CURVE)} kind="cycle" class="sm" /></div>
  </div>

  <!-- 7. Output meter -->
  <div class="graph" style="top:362px">
    <Meter {op} width={184} height={5} />
  </div>
</div>

<style>
  .hdr {
    position: absolute;
    left: 8px;
    right: 8px;
    top: 2px;
    height: 22px;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .hdr .title {
    flex: 1 1 auto;
  }
  .badge {
    padding: 0 5px;
    height: 16px;
    line-height: 16px;
    border-radius: 2px;
    background: var(--modulator);
    color: var(--body);
    font: 600 10px/16px 'Barlow Condensed', sans-serif;
    letter-spacing: 0.06em;
  }
  .badge.carrier {
    background: var(--carrier);
  }
  .row {
    position: absolute;
    left: 8px;
    width: 184px;
    display: flex;
    justify-content: space-between;
  }
  .graph {
    position: absolute;
    left: 8px;
  }
  .freq {
    display: flex;
    align-items: center;
    justify-content: center;
    height: 18px;
    color: var(--legend);
    font: 600 12px/1 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .wide {
    flex-direction: row !important;
    align-items: center;
    justify-content: space-between;
  }
  .wide :global(.big) {
    width: 70px;
    height: 24px;
    font-size: 20px;
  }
  .op :global(.lbl) {
    line-height: 9px;
  }
  .op .f {
    gap: 1px;
  }
  .row :global(.sm) {
    height: 18px;
    font-size: 12px;
  }
</style>

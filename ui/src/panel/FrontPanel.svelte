<script lang="ts">
  // Front panel (§5.2, y 0–260): exact boxes from the spec. DOM order is the
  // Tab order (R-U5): left to right.
  import { store } from '../store.svelte';
  import { webState } from '../web/webstate.svelte';
  import PanelButton from './PanelButton.svelte';
  import Wheel from './Wheel.svelte';
  import Slider from './Slider.svelte';
  import Lcd from '../lcd/Lcd.svelte';
  import Led from '../led/Led.svelte';
  import CartridgeSlot from './CartridgeSlot.svelte';
  import {
    EDIT_LEGEND,
    FUNCTION_LEGEND,
    MODE_BUTTONS,
    NAME_CHARS,
    OPERATOR_LEGEND,
    numberedAria,
  } from './legends';

  const BTN_X0 = 712;
  const PITCH = 29;
  const BTN_W = 26;
  const BTN_H = 36;
  const ROW_Y = [92, 182];
  const LEG = 7.5; // legend line height

  const btnX = (n: number): number => BTN_X0 + ((n - 1) % 16) * PITCH + (PITCH - BTN_W) / 2;
  const btnY = (n: number): number => ROW_Y[n <= 16 ? 0 : 1];

  // Mode buttons: 2 columns × 4 rows in 566, 70, 132, 150.
  const MODE_X = 566;
  const MODE_Y = 70;
  const MODE_CW = 66;
  const MODE_RH = 37.5;
  const MODE_BW = 44;
  const MODE_BH = 16;

  const live = $derived(store.ready && !webState.startCard);
  const numLit = (n: number): boolean => live && (store.lamps.num & (1 << (n - 1))) !== 0;
  const modeBlink = (id: number): boolean => live && (store.lamps.mode & store.lamps.blink & (1 << (id - 33))) !== 0;
  const modeLit = (id: number): boolean => {
    if (!live) return false;
    const bit = 1 << (id - 33);
    if ((store.lamps.mode & bit) === 0) return false;
    return (store.lamps.blink & bit) === 0 || store.blinkOn;
  };
</script>

<div class="front" data-testid="front-panel">
  <!-- Legends (SVG, decorative; the buttons carry the ARIA names) -->
  <svg class="legends" width="1280" height="260" viewBox="0 0 1280 260" aria-hidden="true">
    <text class="leg" x="37" y="236" text-anchor="middle">PITCH</text>
    <text class="leg" x="81" y="236" text-anchor="middle">MODULATION</text>
    <text class="leg" x="142" y="236" text-anchor="middle">VOLUME</text>
    <text class="leg" x="194" y="236" text-anchor="middle">DATA ENTRY</text>
    <text class="leg small" x="258" y="91" text-anchor="middle">NO · OFF · −1 · &lt;</text>
    <text class="leg small" x="258" y="147" text-anchor="middle">YES · ON · +1 · &gt;</text>

    {#each MODE_BUTTONS as m}
      {#each m.lines as line, k}
        <text
          class="leg tiny"
          x={MODE_X + m.col * MODE_CW + MODE_CW / 2}
          y={MODE_Y + m.row * MODE_RH + MODE_RH - MODE_BH - 3 - (m.lines.length - 1 - k) * LEG}
          text-anchor="middle">{line}</text
        >
      {/each}
    {/each}

    <!-- spanning EDIT legend over buttons 1–6 -->
    <text class="leg violet tiny" x={BTN_X0 + 3 * PITCH} y="80" text-anchor="middle">{OPERATOR_LEGEND}</text>
    <path
      d="M{btnX(1)} 88 V84 H{btnX(6) + BTN_W} V88"
      fill="none"
      stroke="var(--violet)"
      stroke-width="1"
    />
    {#each Array.from({ length: 32 }, (_, i) => i + 1) as n}
      {@const cx = btnX(n) + BTN_W / 2}
      {@const top = btnY(n)}
      {#each EDIT_LEGEND[n - 1] as line, k}
        <text class="leg violet tiny" x={cx} y={top - 3 - (EDIT_LEGEND[n - 1].length - 1 - k) * LEG} text-anchor="middle"
          >{line}</text
        >
      {/each}
      {#each FUNCTION_LEGEND[n - 1] as line, k}
        <text class="leg brown tiny" x={cx} y={top + BTN_H + 8 + k * LEG} text-anchor="middle">{line}</text>
      {/each}
    {/each}
  </svg>

  <div class="wordmark" aria-label="VDX 6-OPERATOR FM SYNTHESIZER" role="img">
    <span class="vdx">VDX</span><span class="sub">6-OPERATOR FM SYNTHESIZER</span>
  </div>

  <Wheel kind="bend" x={20} y={70} w={34} h={150} label="PITCH" />
  <Wheel kind="mod" x={64} y={70} w={34} h={150} label="MODULATION" />
  <Slider kind="volume" x={124} y={70} w={36} h={150} label="VOLUME" />
  <Slider kind="data" x={176} y={70} w={36} h={150} label="DATA ENTRY" />

  <PanelButton id={41} x={226} y={96} w={64} h={40} label="NO / OFF / −1 / <" testid="btn-no" />
  <PanelButton id={42} x={226} y={152} w={64} h={40} label="YES / ON / +1 / >" testid="btn-yes" />

  <Led x={316} y={60} v={live ? store.led.v : 0} dot={live && store.led.dot} />
  <Lcd
    x={316}
    y={118}
    l1={store.lcd.l1}
    l2={store.lcd.l2}
    row={store.lcd.row}
    col={store.lcd.col}
    cursor={store.lcd.cursor}
    lit={live && store.lcd.lit}
    blinkOn={store.blinkOn}
  />

  {#each [0, 1] as col}
    {#each MODE_BUTTONS.filter((m) => m.col === col) as m}
      <PanelButton
        id={m.id}
        x={MODE_X + m.col * MODE_CW + (MODE_CW - MODE_BW) / 2}
        y={MODE_Y + m.row * MODE_RH + MODE_RH - MODE_BH - 1}
        w={MODE_BW}
        h={MODE_BH}
        label={m.name}
        salmon={m.salmon}
        lit={modeLit(m.id)}
        blink={modeBlink(m.id)}
      />
    {/each}
  {/each}

  {#each Array.from({ length: 32 }, (_, i) => i + 1) as n}
    <PanelButton id={n} x={btnX(n)} y={btnY(n)} w={BTN_W} h={BTN_H} label={numberedAria(n)} lit={numLit(n)}>
      <span class="num">{n}</span><span class="chr">{NAME_CHARS[n - 1]}</span>
    </PanelButton>
  {/each}

  <CartridgeSlot x={1192} y={60} w={72} h={180} />
  <button tabindex="0"
    type="button"
    class="settings-btn"
    style="left:1192px;top:20px;width:72px;height:22px"
    aria-label="SETTINGS"
    data-testid="open-settings"
    onclick={() => (store.overlay = 'settings')}>SETTINGS</button
  >
</div>

<style>
  .front {
    position: absolute;
    left: 0;
    top: 0;
    width: 1280px;
    height: 260px;
    background: var(--panel);
  }
  .legends {
    position: absolute;
    left: 0;
    top: 0;
    pointer-events: none;
  }
  .leg {
    font: 600 10px 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    fill: var(--legend);
    letter-spacing: 0.04em;
  }
  .leg.small {
    font-size: 9px;
  }
  .leg.tiny {
    font-size: 7.5px;
    letter-spacing: 0.02em;
  }
  .leg.violet {
    fill: var(--violet);
  }
  .leg.brown {
    fill: var(--brown-legend);
  }
  .wordmark {
    position: absolute;
    left: 120px;
    top: 14px;
    width: 420px;
    height: 30px;
    display: flex;
    align-items: baseline;
    gap: 14px;
    color: var(--legend);
    white-space: nowrap;
  }
  .vdx {
    font: 500 28px/30px 'Oswald', sans-serif;
    letter-spacing: 0.12em;
  }
  .sub {
    font: 600 13px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.08em;
  }
  .num {
    position: absolute;
    left: 3px;
    top: 3px;
    color: var(--legend);
    font: 600 10px/1 'Barlow Condensed', sans-serif;
  }
  .chr {
    position: absolute;
    right: 3px;
    bottom: 3px;
    color: var(--body);
    font: 600 10px/1 'Barlow Condensed', sans-serif;
  }
  .settings-btn {
    position: absolute;
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    border: 1px solid var(--legend);
    border-radius: 2px;
    background: transparent;
    color: var(--legend);
    font: 600 10px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.08em;
    cursor: pointer;
  }
</style>

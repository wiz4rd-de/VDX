<script lang="ts">
  // Settings overlay (§5.8): the §4.4 settings, MIDI receive channel and omni,
  // devices (standalone button / web MIDI lists) and the state lines.
  import { tick } from 'svelte';
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { keyboard } from '../input';
  import { midEllipsis } from '../bank/files';
  import type { SettingId } from '../protocol';

  let root: HTMLDivElement | undefined = $state();
  $effect(() => {
    if (root) void tick().then(() => root?.querySelector<HTMLElement>('input, select, button')?.focus());
  });

  const env = $derived(store.env);
  const s = $derived(store.settings);
  const ZOOMS = Array.from({ length: 16 }, (_, i) => 50 + i * 10);

  function set(id: SettingId, v: number): void {
    send({ t: 'setSetting', id, v });
    if (id === 'keyboardPlay') keyboard.setPlay(v === 1);
  }
  function close(): void {
    store.overlay = 'none';
  }
  /** Space is sustain while keyboard play is on (R-K2), so Enter toggles checkboxes and radios. */
  function onKey(ev: KeyboardEvent): void {
    const t = ev.target;
    if (ev.key === 'Enter' && t instanceof HTMLInputElement && (t.type === 'checkbox' || t.type === 'radio')) {
      ev.preventDefault();
      t.click();
    }
  }
  function selectMidi(inputs: string[], output: string | null): void {
    store.env.inputs = store.env.inputs.map((p) => ({ ...p, on: inputs.includes(p.id) }));
    store.env.output = output;
    send({ t: 'midiSelect', inputs, output });
  }
  function toggleInput(id: string, on: boolean): void {
    const ids = env.inputs.filter((p) => (p.id === id ? on : p.on)).map((p) => p.id);
    selectMidi(ids, env.output);
  }
  function setOutput(id: string): void {
    selectMidi(
      env.inputs.filter((p) => p.on).map((p) => p.id),
      id === '' ? null : id,
    );
  }
</script>

<div class="scrim" role="presentation" onpointerdown={close}></div>
<div
  bind:this={root}
  class="overlay"
  style="left:160px;top:180px;width:960px;height:640px"
  role="dialog"
  aria-modal="true"
  aria-label="Settings"
  data-testid="settings"
  tabindex="-1"
  onkeydown={onKey}
>
  <h2 class="otitle">SETTINGS</h2>

  <section class="sec" style="left:24px;top:64px;width:440px">
    <h3>Sound</h3>
    <fieldset>
      <legend>Engine</legend>
      <label><input tabindex="0" type="radio" name="engine" checked={s.engine === 0} data-testid="setting-engine-mk1" onchange={() => set('engine', 0)} /> Mk I</label>
      <label><input tabindex="0" type="radio" name="engine" checked={s.engine === 1} data-testid="setting-engine-modern" onchange={() => set('engine', 1)} /> Modern</label>
    </fieldset>
    <label class="line">
      <input tabindex="0" type="checkbox" checked={s.dac === 1} disabled={s.engine !== 0} data-testid="setting-dac" onchange={(e) => set('dac', e.currentTarget.checked ? 1 : 0)} />
      DAC emulation (Mk I only)
    </label>
    <label class="line">
      <input tabindex="0" type="checkbox" checked={s.hostRate === 1} data-testid="setting-hostRate" onchange={(e) => set('hostRate', e.currentTarget.checked ? 1 : 0)} />
      Render at host rate
    </label>
    <label class="line">
      <input tabindex="0" type="checkbox" checked={s.classicVelocity === 1} data-testid="setting-classicVelocity" onchange={(e) => set('classicVelocity', e.currentTarget.checked ? 1 : 0)} />
      Classic velocity scaling
    </label>

    <h3>Interface</h3>
    <label class="line">
      <input tabindex="0" type="checkbox" checked={s.keyboardPlay === 1} data-testid="setting-keyboardPlay" onchange={(e) => set('keyboardPlay', e.currentTarget.checked ? 1 : 0)} />
      Computer keyboard play
    </label>
    <label class="line">
      Zoom
      <select data-testid="setting-zoom" value={String(s.zoom)} onchange={(e) => set('zoom', Number(e.currentTarget.value))}>
        {#if s.zoom === 0}<option value="0" disabled>Fit</option>{/if}
        {#each ZOOMS as z}<option value={String(z)}>{z} %</option>{/each}
      </select>
    </label>
    <fieldset>
      <legend>Operator order in programmer</legend>
      <label><input tabindex="0" type="radio" name="opOrder" checked={s.opOrder === 0} data-testid="setting-opOrder-0" onchange={() => set('opOrder', 0)} /> OP1→OP6</label>
      <label><input tabindex="0" type="radio" name="opOrder" checked={s.opOrder === 1} data-testid="setting-opOrder-1" onchange={() => set('opOrder', 1)} /> OP6→OP1</label>
    </fieldset>
  </section>

  <section class="sec" style="left:496px;top:64px;width:440px">
    <h3>MIDI</h3>
    <label class="line">
      Receive channel
      <select
        data-testid="setting-midiChannel"
        value={String(store.globals.midiChannel.v)}
        onchange={(e) => send({ t: 'setGlobal', id: 'midiChannel', v: Number(e.currentTarget.value) })}
      >
        {#each Array.from({ length: 16 }, (_, i) => i + 1) as ch}<option value={String(ch)}>{ch}</option>{/each}
      </select>
    </label>
    <label class="line">
      <input
        tabindex="0"
        type="checkbox"
        checked={store.globals.omni.v === 1}
        data-testid="setting-omni"
        onchange={(e) => send({ t: 'setGlobal', id: 'omni', v: e.currentTarget.checked ? 1 : 0 })}
      />
      Omni
    </label>

    {#if env.build === 'standalone'}
      <h3>Devices</h3>
      <button tabindex="0" type="button" class="btn" data-testid="open-devices" onclick={() => send({ t: 'openDevices' })}>Audio/MIDI devices…</button>
    {:else if env.build === 'web'}
      <h3>Devices</h3>
      {#if env.midi === 'unavailable'}
        <p class="note" data-testid="midi-unavailable">MIDI not available — use the computer keyboard.</p>
      {:else}
        {#if env.midi === 'nosysex'}
          <p class="note" data-testid="midi-nosysex">SysEx not permitted — dumps disabled.</p>
        {/if}
        <fieldset class="ports" data-testid="midi-inputs">
          <legend>MIDI inputs</legend>
          {#each env.inputs as port (port.id)}
            <label class="line" title={port.name}>
              <input
                tabindex="0"
                type="checkbox"
                checked={!!port.on}
                data-testid="midi-input-{port.id}"
                onchange={(e) => toggleInput(port.id, e.currentTarget.checked)}
              />
              <span class="port">{midEllipsis(port.name)}</span>
            </label>
          {/each}
        </fieldset>
        <label class="line">
          MIDI output
          <select data-testid="midi-output" value={env.output ?? ''} onchange={(e) => setOutput(e.currentTarget.value)}>
            <option value="">None</option>
            {#each env.outputs as port (port.id)}<option value={port.id} title={port.name}>{midEllipsis(port.name, 36)}</option>{/each}
          </select>
        </label>
      {/if}
    {/if}
  </section>

  <p class="savestatus" role="status" data-testid="settings-status">{store.saveError ?? ''}</p>
  <button tabindex="0" type="button" class="btn closebtn" data-testid="settings-close" onclick={close}>Close</button>
</div>

<style>
  .otitle {
    position: absolute;
    left: 24px;
    top: 20px;
    margin: 0;
    font: 600 18px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.12em;
  }
  .sec {
    position: absolute;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  h3 {
    margin: 8px 0 0;
    font: 600 13px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--brown-legend);
  }
  fieldset {
    margin: 0;
    padding: 0;
    border: none;
    display: flex;
    gap: 18px;
    align-items: center;
    flex-wrap: wrap;
  }
  fieldset.ports {
    flex-direction: column;
    align-items: flex-start;
    gap: 6px;
  }
  legend {
    float: left;
    margin-right: 6px;
    padding: 0;
  }
  label,
  legend,
  .note {
    font: 500 15px/1.2 'Barlow Condensed', sans-serif;
  }
  .line {
    display: flex;
    gap: 8px;
    align-items: center;
    white-space: nowrap;
  }
  .note {
    margin: 0;
  }
  .port {
    overflow: hidden;
    white-space: nowrap;
  }
  .savestatus {
    position: absolute;
    left: 24px;
    bottom: 22px;
    margin: 0;
    font: 500 14px/1 'Barlow Condensed', sans-serif;
  }
  .closebtn {
    position: absolute;
    right: 24px;
    bottom: 16px;
  }
</style>

<script lang="ts">
  // A ParamControl bound to one voice parameter, function parameter or panel
  // global. Sends setParam / setFunc / setGlobal; drags and wheel are
  // coalesced to ≤ 1 message per animation frame per parameter.
  import ParamControl from './ParamControl.svelte';
  import { store } from '../store.svelte';
  import { send, sendSoon, flush } from '../bridge';
  import type { Command, GlobalId } from '../protocol';
  import {
    voiceMax,
    voiceParamName,
    voiceRangeText,
    voiceEntryOffset,
    functionMax,
    functionName,
    functionRangeText,
    GLOBAL_RANGE,
  } from '../params';

  interface Props {
    voice?: number;
    func?: number;
    global?: GlobalId;
    kind?: 'number' | 'toggle' | 'cycle';
    /** Overrides the ARIA name / tooltip name. */
    name?: string;
    disabled?: boolean;
    class?: string;
    style?: string;
  }

  let { voice, func, global, kind = 'number', name, disabled = false, class: cls = '', style = '' }: Props = $props();

  const GLOBAL_NAMES: Record<GlobalId, string> = {
    masterTune: 'Master Tune',
    midiChannel: 'MIDI Channel',
    omni: 'Omni',
    sysInfo: 'SYS INFO AVAIL',
    protectInt: 'Memory Protect Internal',
    protectCrt: 'Memory Protect Cartridge',
    volume: 'Master Volume',
  };

  const meta = $derived.by(() => {
    if (voice !== undefined) {
      const nm = name ?? voiceParamName(voice);
      return {
        value: store.params[voice],
        display: store.paramStr[voice],
        min: 0,
        max: voiceMax(voice),
        offset: voiceEntryOffset(voice),
        name: nm,
        tip: `${nm} (${voiceRangeText(voice)})`,
        testid: `param-${voice}`,
      };
    }
    if (func !== undefined) {
      const nm = name ?? functionName(func);
      return {
        value: store.func[func],
        display: store.funcStr[func],
        min: 0,
        max: functionMax(func),
        offset: 0,
        name: nm,
        tip: `${nm} (${functionRangeText(func)})`,
        testid: `func-${func}`,
      };
    }
    const id = global as GlobalId;
    const r = GLOBAL_RANGE[id];
    const nm = name ?? GLOBAL_NAMES[id];
    return {
      value: store.globals[id].v,
      display: store.globals[id].s,
      min: r.min,
      max: r.max,
      offset: 0,
      name: nm,
      tip: `${nm} (${r.min} – ${r.max})`,
      testid: `global-${id}`,
    };
  });

  function command(v: number): Command {
    if (voice !== undefined) return { t: 'setParam', i: voice, v };
    if (func !== undefined) return { t: 'setFunc', i: func, v };
    return { t: 'setGlobal', id: global as GlobalId, v };
  }

  function onchange(v: number, final: boolean): void {
    if (final) send(command(v));
    else sendSoon(command(v));
  }
</script>

<ParamControl
  value={meta.value}
  display={meta.display}
  min={meta.min}
  max={meta.max}
  offset={meta.offset}
  label={meta.name}
  tip={meta.tip}
  testid={meta.testid}
  ready={store.ready}
  {kind}
  {disabled}
  class={cls}
  {style}
  {onchange}
  onend={flush}
/>

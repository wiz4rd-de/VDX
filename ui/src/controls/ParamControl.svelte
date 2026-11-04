<script lang="ts">
  // A programmer control (R-U1): the value is always visible; double-click or
  // start typing to enter a number (Enter commits, Esc reverts, out-of-range
  // clamps, non-numbers revert); wheel ±1, Shift+wheel ±10; vertical drag
  // 1 step / 4 px, Ctrl/Cmd+drag 1 step / 16 px; the tooltip shows name and
  // range and never changes a value.
  import { tick } from 'svelte';
  import { DragTracker, isEntryStartKey, parseEntry, step, wheelDelta, type NumRange } from './numeric';

  interface Props {
    /** Raw value. */
    value: number;
    /** Display string from the core. */
    display: string;
    min?: number;
    max: number;
    /** Typed number = raw + offset. */
    offset?: number;
    /** ARIA name. */
    label: string;
    /** Tooltip: name and range. */
    tip: string;
    kind?: 'number' | 'toggle' | 'cycle';
    ready?: boolean;
    disabled?: boolean;
    testid?: string;
    class?: string;
    style?: string;
    /** `final` is false for continuous changes (drag, wheel), true for commits. */
    onchange: (v: number, final: boolean) => void;
    /** End of a drag (flush coalesced messages). */
    onend?: () => void;
  }

  let {
    value,
    display,
    min = 0,
    max,
    offset = 0,
    label,
    tip,
    kind = 'number',
    ready = true,
    disabled = false,
    testid,
    class: cls = '',
    style = '',
    onchange,
    onend,
  }: Props = $props();

  let editing = $state(false);
  let text = $state('');
  let inputEl: HTMLInputElement | undefined = $state();
  let root: HTMLDivElement | undefined = $state();
  let drag: DragTracker | null = null;
  let lastDragValue = 0;
  let suppressClick = false;

  const range = $derived<NumRange>({ min, max, offset });
  const shown = $derived(ready ? (display !== '' ? display.trim() : String(value + offset)) : '');
  const on = $derived(value > min);

  function change(v: number, final: boolean): void {
    if (disabled) return;
    if (v !== value) onchange(v, final);
  }

  async function startEdit(initial: string | null): Promise<void> {
    if (disabled || !ready) return;
    text = initial ?? String(value + offset);
    editing = true;
    await tick();
    inputEl?.focus();
    if (initial === null) inputEl?.select();
    else inputEl?.setSelectionRange(text.length, text.length);
  }

  function commit(refocus = true): void {
    if (!editing) return;
    editing = false;
    const v = parseEntry(text, range);
    if (v !== null) change(v, true);
    if (refocus) root?.focus();
  }

  function cancel(): void {
    editing = false;
    root?.focus();
  }

  function onInputKey(ev: KeyboardEvent): void {
    if (ev.key === 'Enter') {
      ev.preventDefault();
      commit();
    } else if (ev.key === 'Escape') {
      ev.preventDefault();
      cancel();
    }
    ev.stopPropagation();
  }

  function onKey(ev: KeyboardEvent): void {
    if (ev.defaultPrevented || editing || disabled) return;
    if (ev.ctrlKey || ev.metaKey || ev.altKey) return;
    let delta = 0;
    switch (ev.key) {
      case 'ArrowUp':
      case 'ArrowRight':
        delta = 1;
        break;
      case 'ArrowDown':
      case 'ArrowLeft':
        delta = -1;
        break;
      case 'PageUp':
        delta = 10;
        break;
      case 'PageDown':
        delta = -10;
        break;
      case 'Home':
        ev.preventDefault();
        change(min, true);
        return;
      case 'End':
        ev.preventDefault();
        change(max, true);
        return;
      case 'Enter':
      case ' ':
        if (kind === 'toggle' && !ev.repeat) {
          ev.preventDefault();
          change(on ? min : max, true);
        } else if (kind === 'cycle' && !ev.repeat) {
          ev.preventDefault();
          change(value >= max ? min : value + 1, true);
        } else if (ev.key === 'Enter') {
          ev.preventDefault();
          void startEdit(null);
        }
        return;
      default:
        if (isEntryStartKey(ev.key)) {
          ev.preventDefault();
          ev.stopPropagation();
          void startEdit(ev.key);
        }
        return;
    }
    ev.preventDefault();
    change(step(value, delta, range), true);
  }

  function onWheel(ev: WheelEvent): void {
    if (editing || disabled || !ready) return;
    const d = wheelDelta(ev.deltaY, ev.deltaX, ev.shiftKey);
    if (d === 0) return;
    ev.preventDefault();
    change(step(value, d, range), false);
  }

  $effect(() => {
    const el = root;
    if (!el) return;
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  });

  function onPointerDown(ev: PointerEvent): void {
    if (ev.button !== 0 || editing || disabled || !ready) return;
    root?.focus();
    root?.setPointerCapture?.(ev.pointerId);
    drag = new DragTracker(ev.clientX, ev.clientY, value, ev.ctrlKey || ev.metaKey, range);
    lastDragValue = value;
    suppressClick = false;
    ev.preventDefault();
  }

  function onPointerMove(ev: PointerEvent): void {
    if (!drag) return;
    const v = drag.move(ev.clientX, ev.clientY, ev.ctrlKey || ev.metaKey);
    if (drag.dragged && v !== lastDragValue) {
      lastDragValue = v;
      change(v, false);
    }
  }

  function endDrag(): void {
    if (!drag) return;
    suppressClick = drag.dragged;
    drag = null;
    onend?.();
  }

  function onClick(ev: MouseEvent): void {
    if (suppressClick) {
      suppressClick = false;
      return;
    }
    if (ev.detail > 1) return;
    if (kind === 'toggle') change(on ? min : max, true);
    else if (kind === 'cycle') change(value >= max ? min : value + 1, true);
  }
</script>

<!-- The role is switch or spinbutton (both interactive); svelte cannot infer a dynamic role. -->
<!-- svelte-ignore a11y_no_noninteractive_tabindex -->
<div
  bind:this={root}
  class="pc {kind} {cls}"
  class:on={kind === 'toggle' && on && ready}
  class:disabled
  {style}
  role={kind === 'toggle' ? 'switch' : 'spinbutton'}
  tabindex={disabled ? -1 : 0}
  aria-label={label}
  aria-disabled={disabled || undefined}
  aria-checked={kind === 'toggle' ? on : undefined}
  aria-valuenow={kind !== 'toggle' && ready ? value + offset : undefined}
  aria-valuemin={kind !== 'toggle' ? min + offset : undefined}
  aria-valuemax={kind !== 'toggle' ? max + offset : undefined}
  aria-valuetext={kind !== 'toggle' && ready ? shown : undefined}
  title={tip}
  data-testid={testid}
  data-numentry="1"
  onkeydown={onKey}
  onpointerdown={onPointerDown}
  onpointermove={onPointerMove}
  onpointerup={endDrag}
  onpointercancel={endDrag}
  onlostpointercapture={endDrag}
  onclick={onClick}
  ondblclick={() => startEdit(null)}
  oncontextmenu={(ev) => ev.preventDefault()}
>
  {#if editing}
    <input
      bind:this={inputEl}
      bind:value={text}
      class="entry"
      type="text"
      inputmode="numeric"
      aria-label={label}
      title={tip}
      onkeydown={onInputKey}
      onblur={() => commit(false)}
      onpointerdown={(ev) => ev.stopPropagation()}
    />
  {:else}
    <span class="val">{shown}</span>
  {/if}
</div>

<style>
  .pc {
    position: relative;
    box-sizing: border-box;
    height: 20px;
    min-width: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    background: var(--body);
    color: var(--legend);
    border: 1px solid var(--panel);
    border-radius: 2px;
    font: 600 13px/1 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    cursor: ns-resize;
    user-select: none;
    touch-action: none;
    white-space: nowrap;
    overflow: hidden;
  }
  .pc.toggle,
  .pc.cycle {
    cursor: pointer;
  }
  .pc.toggle.on {
    background: var(--dx-green);
    border-color: var(--dx-green);
    color: var(--body);
  }
  .pc.disabled {
    opacity: 0.45;
    cursor: default;
  }
  .val {
    pointer-events: none;
  }
  .entry {
    position: absolute;
    inset: 0;
    width: 100%;
    box-sizing: border-box;
    border: none;
    outline: none;
    padding: 0 3px;
    background: var(--legend);
    color: var(--body);
    font: inherit;
    text-align: center;
  }
</style>

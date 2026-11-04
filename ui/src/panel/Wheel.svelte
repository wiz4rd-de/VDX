<script lang="ts">
  // Pitch-bend and mod wheels (§5.2). The bend wheel springs back to centre on
  // release, including a release outside the window. Both follow the `wheels`
  // event (received MIDI) when not being dragged.
  import { store } from '../store.svelte';
  import { sendSoon, flush } from '../bridge';
  import { view } from '../ui.svelte';

  interface Props {
    kind: 'bend' | 'mod';
    x: number;
    y: number;
    w: number;
    h: number;
    label: string;
  }
  let { kind, x, y, w, h, label }: Props = $props();

  const MAX = $derived(kind === 'bend' ? 16383 : 127);
  let dragging = $state(false);
  let local = $state(0);
  let startY = 0;
  let startV = 0;
  let keyHeld = false;

  const storeValue = $derived(kind === 'bend' ? store.wheels.bend : store.wheels.mod);
  const value = $derived(dragging ? local : storeValue);
  const pos = $derived(Math.min(1, Math.max(0, value / MAX)));
  const travel = $derived(h - 20);

  function emit(v: number): void {
    local = v;
    if (kind === 'bend') sendSoon({ t: 'bend', v });
    else sendSoon({ t: 'cc', cc: 1, v });
  }

  function onDown(ev: PointerEvent): void {
    if (ev.button !== 0) return;
    (ev.currentTarget as HTMLElement).setPointerCapture?.(ev.pointerId);
    (ev.currentTarget as HTMLElement).focus();
    dragging = true;
    startY = ev.clientY;
    startV = storeValue;
    local = startV;
    ev.preventDefault();
  }
  function onMove(ev: PointerEvent): void {
    if (!dragging) return;
    const dy = (startY - ev.clientY) / view.scale;
    const v = Math.round(Math.min(MAX, Math.max(0, startV + (dy / travel) * MAX)));
    if (v !== local) emit(v);
  }
  function onUp(): void {
    if (!dragging) return;
    dragging = false;
    if (kind === 'bend') sendSoon({ t: 'bend', v: 8192 });
    flush();
  }
  function onKeyDown(ev: KeyboardEvent): void {
    if (ev.defaultPrevented || ev.altKey || ev.ctrlKey || ev.metaKey) return;
    const up = ev.key === 'ArrowUp' || ev.key === 'ArrowRight';
    const down = ev.key === 'ArrowDown' || ev.key === 'ArrowLeft';
    if (!up && !down) return;
    ev.preventDefault();
    if (kind === 'bend') {
      keyHeld = true;
      sendSoon({ t: 'bend', v: up ? 16383 : 0 });
    } else {
      sendSoon({ t: 'cc', cc: 1, v: Math.min(127, Math.max(0, storeValue + (up ? 8 : -8))) });
    }
  }
  function onKeyUp(): void {
    if (kind === 'bend' && keyHeld) {
      keyHeld = false;
      sendSoon({ t: 'bend', v: 8192 });
    }
  }
  function onBlur(): void {
    onUp();
    onKeyUp();
  }

  // A release outside the window (focus lost while dragging) also springs back.
  $effect(() => {
    window.addEventListener('blur', onBlur);
    return () => window.removeEventListener('blur', onBlur);
  });

  // Ridges scroll with the wheel position.
  const ridges = $derived.by(() => {
    const out: number[] = [];
    const pitch = 7;
    const shift = (pos * travel * 1.6) % pitch;
    for (let yy = 6 + shift; yy < h - 6; yy += pitch) out.push(yy);
    return out;
  });
</script>

<div
  class="wheel"
  style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"
  role="slider"
  tabindex="0"
  aria-label={label}
  aria-orientation="vertical"
  aria-valuemin={0}
  aria-valuemax={MAX}
  aria-valuenow={value}
  data-testid={kind === 'bend' ? 'wheel-bend' : 'wheel-mod'}
  data-value={value}
  onpointerdown={onDown}
  onpointermove={onMove}
  onpointerup={onUp}
  onpointercancel={onUp}
  onlostpointercapture={onUp}
  onkeydown={onKeyDown}
  onkeyup={onKeyUp}
  onblur={onBlur}
>
  <svg width={w} height={h} viewBox="0 0 {w} {h}" aria-hidden="true">
    <rect x="0" y="0" width={w} height={h} rx="3" fill="var(--body)" />
    <rect x="5" y="4" width={w - 10} height={h - 8} rx="4" fill="var(--programmer)" />
    {#each ridges as ry}
      <line x1="7" x2={w - 7} y1={ry} y2={ry} stroke="var(--panel)" stroke-width="2" />
    {/each}
    {#if kind === 'bend'}
      <line x1="1" x2="4" y1={h / 2} y2={h / 2} stroke="var(--legend)" stroke-width="1" />
      <line x1={w - 4} x2={w - 1} y1={h / 2} y2={h / 2} stroke="var(--legend)" stroke-width="1" />
    {/if}
    <rect x="5" y={10 + (1 - pos) * travel - 2} width={w - 10} height="4" fill="var(--legend)" />
  </svg>
</div>

<style>
  .wheel {
    position: absolute;
    cursor: ns-resize;
    touch-action: none;
  }
  svg {
    display: block;
  }
</style>

<script lang="ts">
  // Volume and data-entry sliders (§5.2). Volume sets the panel global
  // `volume` (CC7 moves it); data entry sends `dataEntry` and its thumb
  // follows the core's `dataEntry` events when not being dragged (F-2).
  import { store } from '../store.svelte';
  import { sendSoon, flush } from '../bridge';
  import { view } from '../ui.svelte';

  interface Props {
    kind: 'volume' | 'data';
    x: number;
    y: number;
    w: number;
    h: number;
    label: string;
  }
  let { kind, x, y, w, h, label }: Props = $props();

  const THUMB_H = 14;
  const travel = $derived(h - THUMB_H - 8);
  let dragging = $state(false);
  let local = $state(0);
  let startY = 0;
  let startPos = 0;

  const storePos = $derived(kind === 'volume' ? store.globals.volume.v / 127 : store.dataEntry);
  const pos = $derived(Math.min(1, Math.max(0, dragging ? local : storePos)));

  function emit(p: number): void {
    local = p;
    if (kind === 'volume') sendSoon({ t: 'setGlobal', id: 'volume', v: Math.round(p * 127) });
    else sendSoon({ t: 'dataEntry', pos: p });
  }
  function onDown(ev: PointerEvent): void {
    if (ev.button !== 0) return;
    (ev.currentTarget as HTMLElement).setPointerCapture?.(ev.pointerId);
    (ev.currentTarget as HTMLElement).focus();
    dragging = true;
    startY = ev.clientY;
    startPos = storePos;
    local = startPos;
    ev.preventDefault();
  }
  function onMove(ev: PointerEvent): void {
    if (!dragging) return;
    const dy = (startY - ev.clientY) / view.scale;
    let p = Math.min(1, Math.max(0, startPos + dy / travel));
    if (kind === 'volume') p = Math.round(p * 127) / 127;
    if (p !== local) emit(p);
  }
  function onUp(): void {
    if (!dragging) return;
    dragging = false;
    flush();
  }
  function onKeyDown(ev: KeyboardEvent): void {
    if (ev.defaultPrevented || ev.altKey || ev.ctrlKey || ev.metaKey) return;
    let d = 0;
    if (ev.key === 'ArrowUp' || ev.key === 'ArrowRight') d = 1;
    else if (ev.key === 'ArrowDown' || ev.key === 'ArrowLeft') d = -1;
    else return;
    ev.preventDefault();
    const stepSize = kind === 'volume' ? 1 / 127 : 0.01;
    const p = Math.min(1, Math.max(0, storePos + d * stepSize));
    if (kind === 'volume') sendSoon({ t: 'setGlobal', id: 'volume', v: Math.round(p * 127) });
    else sendSoon({ t: 'dataEntry', pos: p });
    flush();
  }
</script>

<div
  class="slider"
  style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"
  role="slider"
  tabindex="0"
  aria-label={label}
  aria-orientation="vertical"
  aria-valuemin={0}
  aria-valuemax={kind === 'volume' ? 127 : 100}
  aria-valuenow={kind === 'volume' ? Math.round(pos * 127) : Math.round(pos * 100)}
  data-testid={kind === 'volume' ? 'slider-volume' : 'slider-data'}
  data-pos={pos.toFixed(4)}
  onpointerdown={onDown}
  onpointermove={onMove}
  onpointerup={onUp}
  onpointercancel={onUp}
  onlostpointercapture={onUp}
  onkeydown={onKeyDown}
  onblur={onUp}
>
  <svg width={w} height={h} viewBox="0 0 {w} {h}" aria-hidden="true">
    <rect x="0" y="0" width={w} height={h} rx="3" fill="var(--body)" />
    <rect x={w / 2 - 2} y="6" width="4" height={h - 12} rx="2" fill="var(--programmer)" />
    {#each [0, 0.25, 0.5, 0.75, 1] as t}
      <line x1="3" x2="8" y1={4 + THUMB_H / 2 + (1 - t) * travel} y2={4 + THUMB_H / 2 + (1 - t) * travel} stroke="var(--panel)" />
      <line x1={w - 8} x2={w - 3} y1={4 + THUMB_H / 2 + (1 - t) * travel} y2={4 + THUMB_H / 2 + (1 - t) * travel} stroke="var(--panel)" />
    {/each}
    <rect x="4" y={4 + (1 - pos) * travel} width={w - 8} height={THUMB_H} rx="2" fill="var(--panel)" stroke="var(--legend)" stroke-width="1" />
    <line x1="6" x2={w - 6} y1={4 + (1 - pos) * travel + THUMB_H / 2} y2={4 + (1 - pos) * travel + THUMB_H / 2} stroke="var(--legend)" stroke-width="1.5" />
  </svg>
</div>

<style>
  .slider {
    position: absolute;
    cursor: ns-resize;
    touch-action: none;
  }
  svg {
    display: block;
  }
</style>

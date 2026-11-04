// Shared pointer-drag helper for the graph handles (EG points, break point).
// Deltas are converted from screen px to logical canvas px.
import { view } from '../ui.svelte';

export interface DragHandlers {
  start(): void;
  move(dx: number, dy: number): void;
  end(): void;
}

export function beginDrag(ev: PointerEvent, h: DragHandlers): void {
  if (ev.button !== 0) return;
  ev.preventDefault();
  ev.stopPropagation();
  const el = ev.currentTarget as Element;
  el.setPointerCapture?.(ev.pointerId);
  const x0 = ev.clientX;
  const y0 = ev.clientY;
  h.start();
  const onMove = (e: PointerEvent): void => h.move((e.clientX - x0) / view.scale, (e.clientY - y0) / view.scale);
  const onUp = (): void => {
    el.removeEventListener('pointermove', onMove as EventListener);
    el.removeEventListener('pointerup', onUp);
    el.removeEventListener('pointercancel', onUp);
    el.removeEventListener('lostpointercapture', onUp);
    h.end();
  };
  el.addEventListener('pointermove', onMove as EventListener);
  el.addEventListener('pointerup', onUp);
  el.addEventListener('pointercancel', onUp);
  el.addEventListener('lostpointercapture', onUp);
}

export const clamp99 = (v: number): number => Math.min(99, Math.max(0, Math.round(v)));

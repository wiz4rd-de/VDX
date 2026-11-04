// Converts window (client) coordinates to logical canvas coordinates.
import { view } from '../ui.svelte';

export function toCanvas(clientX: number, clientY: number): { x: number; y: number } {
  const el = document.querySelector('[data-canvas]');
  if (!el) return { x: clientX, y: clientY };
  const r = el.getBoundingClientRect();
  return { x: (clientX - r.left) / view.scale, y: (clientY - r.top) / view.scale };
}

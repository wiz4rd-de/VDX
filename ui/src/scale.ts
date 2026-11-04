// Canvas scaling (§5.1): one fixed 1280 × 1000 layout, scaled uniformly to
// fit the window, clamped to 50–200 %. Web: the Zoom setting caps the scale.
// Desktop: the native side applies the zoom to the window; the UI just fits.
export const CANVAS_W = 1280;
export const CANVAS_H = 1000;
export const MIN_SCALE = 0.5;
export const MAX_SCALE = 2;

export function fitScale(winW: number, winH: number, build: string, zoom: number): number {
  let s = Math.min(winW / CANVAS_W, winH / CANVAS_H);
  if (!Number.isFinite(s) || s <= 0) s = 1;
  let cap = MAX_SCALE;
  if (build === 'web' && zoom >= 50 && zoom <= 200) cap = zoom / 100;
  return Math.min(cap, Math.max(MIN_SCALE, s));
}

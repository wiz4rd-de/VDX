// R-U1 control logic, kept pure so it can be unit-tested without a DOM.

export interface NumRange {
  min: number;
  max: number;
  /** Typed number = raw value + offset (algorithm 1–32: +1; detune −7…+7: −7). */
  offset?: number;
}

export const PX_PER_STEP = 4; // vertical drag: 1 step per 4 px
export const PX_PER_STEP_FINE = 16; // Ctrl/Cmd + drag: 1 step per 16 px
export const WHEEL_STEP = 1;
export const WHEEL_STEP_SHIFT = 10;
/** Pointer movement (px) below which a press counts as a click, not a drag. */
export const CLICK_SLOP = 3;

export const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));

const NUMBER_RE = /^[+-]?(\d+(\.\d*)?|\.\d+)$/;

/**
 * Parses typed entry (Enter commits). Returns the raw value clamped to the
 * range, or null for anything that is not a number (the control reverts).
 */
export function parseEntry(text: string, r: NumRange): number | null {
  const t = text.trim().replace(/^−/, '-');
  if (!NUMBER_RE.test(t)) return null;
  const n = Number(t);
  if (!Number.isFinite(n)) return null;
  return clamp(Math.round(n) - (r.offset ?? 0), r.min, r.max);
}

/** Wheel: ±1 per event, Shift+wheel ±10; scrolling up increases. */
export function wheelDelta(deltaY: number, deltaX: number, shift: boolean): number {
  // Shift+wheel is reported on the horizontal axis by some browsers.
  const d = deltaY !== 0 ? deltaY : deltaX;
  if (d === 0) return 0;
  return (d < 0 ? 1 : -1) * (shift ? WHEEL_STEP_SHIFT : WHEEL_STEP);
}

/** Steps for an upward drag distance (px, positive = up). */
export function dragSteps(upPx: number, fine: boolean): number {
  return Math.trunc(upPx / (fine ? PX_PER_STEP_FINE : PX_PER_STEP));
}

/** Applies a step delta to a raw value inside its range. */
export function step(value: number, delta: number, r: NumRange): number {
  return clamp(value + delta, r.min, r.max);
}

/** Keys that start typed entry on a focused control ("start typing", R-U1). */
export function isEntryStartKey(key: string): boolean {
  return key.length === 1 && /[0-9+\-.]/.test(key);
}

/**
 * Vertical drag tracker. The start point is re-based whenever the fine
 * modifier changes, so switching between 4 px and 16 px per step mid-drag
 * never jumps.
 */
export class DragTracker {
  private startY: number;
  private startValue: number;
  private fine: boolean;
  private maxMove = 0;
  private readonly startX: number;
  private readonly range: NumRange;

  constructor(x: number, y: number, value: number, fine: boolean, range: NumRange) {
    this.startX = x;
    this.startY = y;
    this.startValue = value;
    this.fine = fine;
    this.range = range;
  }

  /** Returns the value for the pointer at (x, y). */
  move(x: number, y: number, fine: boolean): number {
    this.maxMove = Math.max(this.maxMove, Math.abs(x - this.startX), Math.abs(y - this.startY));
    if (fine !== this.fine) {
      this.startValue = this.valueAt(y);
      this.startY = y;
      this.fine = fine;
    }
    return this.valueAt(y);
  }

  private valueAt(y: number): number {
    return step(this.startValue, dragSteps(this.startY - y, this.fine), this.range);
  }

  /** True once the pointer moved far enough to count as a drag. */
  get dragged(): boolean {
    return this.maxMove >= CLICK_SLOP;
  }
}

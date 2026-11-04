// Computer keyboard (SPEC §7.6, R-K1 / R-K2). Pure module: it receives
// keyboard-event-like objects and emits protocol commands through an injected
// `send`. Keys are matched by `KeyboardEvent.code` (physical position).
import type { BuildKind, Command } from './protocol';

export interface KeyEventLike {
  code: string;
  repeat?: boolean;
  ctrlKey?: boolean;
  metaKey?: boolean;
  altKey?: boolean;
  shiftKey?: boolean;
  isComposing?: boolean;
  target?: unknown;
  preventDefault?: () => void;
}

export interface KeyboardOptions {
  send: (cmd: Command) => void;
  /** Build: play defaults to on in web/standalone and off in the plugin (R-K2). */
  build?: BuildKind;
  /** Explicit initial play state (overrides the build default). */
  play?: boolean;
  /** True when `target` is a text field: the mapping is suspended entirely. */
  isTextField?: (target: unknown) => boolean;
  /** True when `target` is a focused numeric control: digit keys start typed entry (R-U1). */
  isNumberEntry?: (target: unknown) => boolean;
  /** Called when the play state changes; `byUser` is true for M / F9. */
  onPlayChange?: (on: boolean, byUser: boolean) => void;
}

/** White and black keys: semitone offset from the base note (R-K1). */
export const NOTE_KEYS: Readonly<Record<string, number>> = {
  KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4, KeyF: 5, KeyT: 6, KeyG: 7,
  KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11, KeyK: 12, KeyO: 13, KeyL: 14,
  KeyP: 15, Semicolon: 16, Quote: 17,
};

/** Mod wheel keys 3–8 (R-K1). */
export const MOD_KEYS: Readonly<Record<string, number>> = {
  Digit3: 0, Digit4: 25, Digit5: 51, Digit6: 76, Digit7: 102, Digit8: 127,
};

export const BEND_DOWN = 0;
export const BEND_UP = 16383;
export const BEND_CENTRE = 8192;
export const BASE_DEFAULT = 60; // C3
export const BASE_MIN = 0;
export const BASE_MAX = 108;
export const VELOCITY_DEFAULT = 100;

const OTHER_KEYS = new Set(['KeyZ', 'KeyX', 'KeyC', 'KeyV', 'Space', 'Digit1', 'Digit2']);
const TOGGLE_KEYS = new Set(['KeyM', 'F9']);
const DIGIT_CODES = new Set([
  'Digit0', 'Digit1', 'Digit2', 'Digit3', 'Digit4', 'Digit5', 'Digit6', 'Digit7', 'Digit8', 'Digit9',
]);

/** True for every code the map captures while play is on. */
export function isMappedCode(code: string): boolean {
  return code in NOTE_KEYS || code in MOD_KEYS || OTHER_KEYS.has(code) || TOGGLE_KEYS.has(code);
}

const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));

export class ComputerKeyboard {
  play: boolean;
  base = BASE_DEFAULT;
  velocity = VELOCITY_DEFAULT;
  /** code → note sounding for that key (stored at key-down, R-K2). */
  readonly held = new Map<string, number>();
  sustain = false;
  private readonly bendKeys: string[] = [];
  private readonly opts: KeyboardOptions;

  constructor(opts: KeyboardOptions) {
    this.opts = opts;
    this.play = opts.play ?? opts.build !== 'plugin';
  }

  private send(cmd: Command): void {
    this.opts.send(cmd);
  }

  /** Handles a keydown. Returns true when the key was captured (preventDefault called). */
  keydown(ev: KeyEventLike): boolean {
    if (this.opts.isTextField?.(ev.target)) return false;
    if (ev.ctrlKey || ev.metaKey || ev.altKey || ev.isComposing) return false;
    const code = ev.code;
    if (TOGGLE_KEYS.has(code)) {
      ev.preventDefault?.();
      if (!ev.repeat) this.setPlay(!this.play, true);
      return true;
    }
    if (!this.play || !isMappedCode(code)) return false;
    if (DIGIT_CODES.has(code) && this.opts.isNumberEntry?.(ev.target)) return false;
    ev.preventDefault?.();
    if (ev.repeat) return true;

    if (code in NOTE_KEYS) {
      if (this.held.has(code)) return true;
      const note = this.base + NOTE_KEYS[code];
      if (note > 127) return true;
      this.held.set(code, note);
      this.send({ t: 'noteOn', note, vel: this.velocity });
      return true;
    }
    if (code in MOD_KEYS) {
      this.send({ t: 'cc', cc: 1, v: MOD_KEYS[code] });
      return true;
    }
    switch (code) {
      case 'KeyZ':
        this.base = clamp(this.base - 12, BASE_MIN, BASE_MAX);
        break;
      case 'KeyX':
        this.base = clamp(this.base + 12, BASE_MIN, BASE_MAX);
        break;
      case 'KeyC':
        this.velocity = clamp(this.velocity - 20, 1, 127);
        break;
      case 'KeyV':
        this.velocity = clamp(this.velocity + 20, 1, 127);
        break;
      case 'Space':
        if (!this.sustain) {
          this.sustain = true;
          this.send({ t: 'cc', cc: 64, v: 127 });
        }
        break;
      case 'Digit1':
      case 'Digit2':
        if (!this.bendKeys.includes(code)) this.bendKeys.push(code);
        this.send({ t: 'bend', v: code === 'Digit1' ? BEND_DOWN : BEND_UP });
        break;
    }
    return true;
  }

  /** Handles a keyup. Note-offs, sustain and bend releases happen regardless of modifiers. */
  keyup(ev: KeyEventLike): boolean {
    const code = ev.code;
    let handled = false;
    const note = this.held.get(code);
    if (note !== undefined) {
      this.held.delete(code);
      this.send({ t: 'noteOff', note });
      handled = true;
    } else if (code === 'Space' && this.sustain) {
      this.sustain = false;
      this.send({ t: 'cc', cc: 64, v: 0 });
      handled = true;
    } else if ((code === 'Digit1' || code === 'Digit2') && this.bendKeys.includes(code)) {
      this.bendKeys.splice(this.bendKeys.indexOf(code), 1);
      const other = this.bendKeys[this.bendKeys.length - 1];
      this.send({ t: 'bend', v: other === undefined ? BEND_CENTRE : other === 'Digit1' ? BEND_DOWN : BEND_UP });
      handled = true;
    }
    const textField = this.opts.isTextField?.(ev.target) ?? false;
    if ((handled || (this.play && isMappedCode(code))) && !textField) ev.preventDefault?.();
    return handled;
  }

  /** Releases every held key, sustain and bend (blur, play off, text focus, editor close). */
  releaseAll(): void {
    for (const note of this.held.values()) this.send({ t: 'noteOff', note });
    this.held.clear();
    if (this.sustain) {
      this.sustain = false;
      this.send({ t: 'cc', cc: 64, v: 0 });
    }
    if (this.bendKeys.length > 0) {
      this.bendKeys.length = 0;
      this.send({ t: 'bend', v: BEND_CENTRE });
    }
  }

  /** Window blur. */
  blur(): void {
    this.releaseAll();
  }

  /** A text field gained focus. */
  textFieldFocused(): void {
    this.releaseAll();
  }

  /** Sets the play state. Turning play off releases all keys. */
  setPlay(on: boolean, byUser = false): void {
    if (on === this.play) return;
    this.play = on;
    if (!on) this.releaseAll();
    this.opts.onPlayChange?.(on, byUser);
  }
}

export function createKeyboard(opts: KeyboardOptions): ComputerKeyboard {
  return new ComputerKeyboard(opts);
}

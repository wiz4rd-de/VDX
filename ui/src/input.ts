// Global keyboard wiring: start-card guard (§5.9), the computer keyboard
// (R-K1/R-K2, capture phase) and the shortcuts (R-U4, bubble phase).
import { ComputerKeyboard } from './keyboard';
import { send } from './bridge';
import { store } from './store.svelte';
import { menus } from './ui.svelte';
import { webState, dismissStartCard } from './web/webstate.svelte';
import { maskBit, G } from './params';

const TEXT_INPUT_TYPES = new Set(['text', 'search', 'url', 'tel', 'email', 'password', 'number']);

/** True for text fields: a focused text field suspends the key map (R-K2). */
export function isTextField(target: unknown): boolean {
  if (typeof HTMLElement === 'undefined' || !(target instanceof HTMLElement)) return false;
  if (target instanceof HTMLInputElement) return TEXT_INPUT_TYPES.has(target.type);
  if (target instanceof HTMLTextAreaElement) return true;
  return target.isContentEditable;
}

/** True for a focused programmer numeric control (digits start typed entry, R-U1). */
export function isNumberEntry(target: unknown): boolean {
  return typeof HTMLElement !== 'undefined' && target instanceof HTMLElement && target.dataset.numentry === '1';
}

export const keyboard = new ComputerKeyboard({
  send,
  play: true,
  isTextField,
  isNumberEntry,
  onPlayChange(on, byUser) {
    if (byUser) send({ t: 'setSetting', id: 'keyboardPlay', v: on ? 1 : 0 });
  },
});

/** Focuses an element by selector (shortcut targets). */
function focusSelector(sel: string): void {
  const el = document.querySelector<HTMLElement>(sel);
  el?.focus();
}

export function closeOverlay(): boolean {
  if (menus.op || menus.voice) {
    menus.op = null;
    menus.voice = false;
    return true;
  }
  if (store.importChoice) {
    send({ t: 'importCancel' });
    store.importChoice = null;
    return true;
  }
  if (store.overlay !== 'none') {
    store.overlay = 'none';
    return true;
  }
  return false;
}

/** R-U4 shortcuts. Returns true when handled. Shortcuts never play notes. */
export function handleShortcut(ev: KeyboardEvent): boolean {
  if (ev.code === 'Escape' && !ev.altKey && !ev.ctrlKey && !ev.metaKey) return closeOverlay();
  if (!ev.altKey || ev.ctrlKey || ev.metaKey) return false;
  const m = /^Digit([1-6])$/.exec(ev.code);
  if (m) {
    const op = Number(m[1]);
    if (ev.shiftKey) {
      send({ t: 'setParam', i: G.OP_MASK, v: store.params[G.OP_MASK] ^ maskBit(op) });
    } else {
      focusSelector(`[data-op-panel="${op}"]`);
      send({ t: 'selectOp', op });
    }
    return true;
  }
  if (ev.shiftKey) return false;
  switch (ev.code) {
    case 'KeyG':
      focusSelector('[data-block="voice"]');
      return true;
    case 'KeyB':
      store.overlay = 'bank';
      return true;
    case 'Comma':
      store.overlay = 'settings';
      return true;
  }
  return false;
}

let swallowKeyUp: string | null = null;

function onKeyDownCapture(ev: KeyboardEvent): void {
  if (webState.fatal) return;
  if (webState.startCard) {
    ev.preventDefault();
    ev.stopImmediatePropagation();
    swallowKeyUp = ev.code;
    dismissStartCard();
    return;
  }
  keyboard.keydown(ev);
}

function onKeyUpCapture(ev: KeyboardEvent): void {
  if (swallowKeyUp !== null && ev.code === swallowKeyUp) {
    swallowKeyUp = null;
    ev.preventDefault();
    ev.stopImmediatePropagation();
    return;
  }
  keyboard.keyup(ev);
}

function onKeyDownBubble(ev: KeyboardEvent): void {
  if (ev.defaultPrevented || webState.startCard || webState.fatal) return;
  if (handleShortcut(ev)) ev.preventDefault();
}

let installed = false;

export function installGlobalInput(): void {
  if (installed) return;
  installed = true;
  window.addEventListener('keydown', onKeyDownCapture, true);
  window.addEventListener('keyup', onKeyUpCapture, true);
  window.addEventListener('keydown', onKeyDownBubble);
  window.addEventListener('blur', () => keyboard.blur());
  window.addEventListener('focusin', (ev) => {
    if (isTextField(ev.target)) keyboard.textFieldFocused();
  });
  const closing = (): void => {
    keyboard.releaseAll();
    send({ t: 'releaseAll' });
    if (store.env.build !== 'web') send({ t: 'editorClosing' });
  };
  window.addEventListener('beforeunload', closing);
}

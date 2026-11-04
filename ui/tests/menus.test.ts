// @vitest-environment jsdom
import { describe, it, expect, beforeEach, vi } from 'vitest';
import { render, fireEvent, cleanup } from '@testing-library/svelte';
import { tick } from 'svelte';
import OpMenu from '../src/programmer/OpMenu.svelte';
import VoiceMenu from '../src/programmer/VoiceMenu.svelte';
import { resetBridge, setBackend } from '../src/bridge';
import { store } from '../src/store.svelte';
import { menus, opClipboard } from '../src/ui.svelte';
import { handleShortcut, keyboard } from '../src/input';
import { FakeBackend } from './fake-backend';
import { INIT_OPERATOR, opBase, G } from '../src/params';

function connect(): FakeBackend {
  resetBridge();
  const fake = new FakeBackend({ sync: true });
  setBackend(fake, store);
  fake.sent.length = 0;
  return fake;
}

function key(code: string, mods: Partial<KeyboardEventInit> = {}): KeyboardEvent {
  return new KeyboardEvent('keydown', { code, bubbles: true, cancelable: true, ...mods });
}

describe('R-U2 operator context menu', () => {
  beforeEach(() => {
    cleanup();
    opClipboard.bytes = null;
  });

  it('R-U2 copy / paste, swap, EG copy and init operator send the R-U2 commands', async () => {
    const fake = connect();
    render(OpMenu);
    menus.op = { op: 2, x: 10, y: 10 };
    await tick();
    expect((document.querySelector('[data-testid=op-menu-paste]') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.click(document.querySelector('[data-testid=op-menu-copy]')!);
    expect(menus.op).toBeNull();
    const op2 = store.params.slice(opBase(2), opBase(2) + 21);
    expect(opClipboard.bytes).toEqual(op2);
    menus.op = { op: 5, x: 10, y: 10 };
    await tick();
    await fireEvent.click(document.querySelector('[data-testid=op-menu-paste]')!);
    menus.op = { op: 5, x: 10, y: 10 };
    await tick();
    expect(document.querySelector('[data-testid=op-menu-swap-5]')).toBeNull();
    await fireEvent.click(document.querySelector('[data-testid=op-menu-swap-1]')!);
    menus.op = { op: 5, x: 10, y: 10 };
    await tick();
    await fireEvent.click(document.querySelector('[data-testid=op-menu-egcopy-3]')!);
    menus.op = { op: 5, x: 10, y: 10 };
    await tick();
    await fireEvent.click(document.querySelector('[data-testid=op-menu-init]')!);
    expect(fake.sent).toEqual([
      { t: 'writeOp', op: 5, bytes: op2 },
      { t: 'swapOps', a: 5, b: 1 },
      { t: 'egCopy', from: 5, to: 3 },
      { t: 'writeOp', op: 5, bytes: [...INIT_OPERATOR] },
    ]);
  });
});

describe('R-U3 voice menu', () => {
  beforeEach(() => cleanup());

  it('R-U3 Init voice, Compare (disabled while CLEAN), Randomise with seed Date.now() & 0x7fffffff', async () => {
    const fake = connect();
    render(VoiceMenu);
    store.memory.edited = false;
    store.panel.edited = false;
    store.panel.comparing = false;
    menus.voice = true;
    await tick();
    expect((document.querySelector('[data-testid=voice-menu-compare]') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.click(document.querySelector('[data-testid=voice-menu-init]')!);
    expect(menus.voice).toBe(false);
    store.memory.edited = true;
    menus.voice = true;
    await tick();
    const compare = document.querySelector('[data-testid=voice-menu-compare]') as HTMLButtonElement;
    expect(compare.disabled).toBe(false);
    await fireEvent.click(compare);
    vi.spyOn(Date, 'now').mockReturnValue(0x1_2345_6789);
    menus.voice = true;
    await tick();
    await fireEvent.click(document.querySelector('[data-testid=voice-menu-randomise]')!);
    vi.restoreAllMocks();
    expect(fake.sent).toEqual([{ t: 'initVoice' }, { t: 'compare' }, { t: 'randomise', seed: 0x2345_6789 }]);
  });
});

describe('R-U4 shortcuts', () => {
  beforeEach(() => {
    cleanup();
    store.overlay = 'none';
    menus.op = null;
    menus.voice = false;
  });

  it('R-U4 every shortcut acts and no note is played', () => {
    const fake = connect();
    keyboard.setPlay(true);
    const panel = document.createElement('div');
    panel.dataset.opPanel = '3';
    panel.tabIndex = -1;
    const voice = document.createElement('div');
    voice.dataset.block = 'voice';
    voice.tabIndex = -1;
    document.body.append(panel, voice);

    store.params[G.OP_MASK] = 0x3f;
    for (const ev of [key('Digit3', { altKey: true }), key('Digit2', { altKey: true, shiftKey: true })]) {
      expect(keyboard.keydown(ev)).toBe(false);
      expect(handleShortcut(ev)).toBe(true);
    }
    expect(document.activeElement).toBe(panel);
    expect(fake.sent).toEqual([
      { t: 'selectOp', op: 3 },
      { t: 'setParam', i: G.OP_MASK, v: 0x3f ^ 0b010000 },
    ]);

    expect(handleShortcut(key('KeyG', { altKey: true }))).toBe(true);
    expect(document.activeElement).toBe(voice);
    expect(handleShortcut(key('KeyB', { altKey: true }))).toBe(true);
    expect(store.overlay).toBe('bank');
    expect(handleShortcut(key('Escape'))).toBe(true);
    expect(store.overlay).toBe('none');
    expect(handleShortcut(key('Comma', { altKey: true }))).toBe(true);
    expect(store.overlay).toBe('settings');
    expect(handleShortcut(key('Escape'))).toBe(true);
    expect(store.overlay).toBe('none');
    expect(handleShortcut(key('Escape'))).toBe(false);
    // Alt+letter is never played as a note (R-K2), and no note-on was sent at all.
    expect(keyboard.keydown(key('KeyA', { altKey: true }))).toBe(false);
    expect(fake.sent.filter((c) => c.t === 'noteOn')).toEqual([]);
    panel.remove();
    voice.remove();
  });
});

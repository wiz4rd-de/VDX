// @vitest-environment jsdom
import { describe, it, expect, beforeEach, vi } from 'vitest';
import { render, fireEvent, cleanup } from '@testing-library/svelte';
import { tick } from 'svelte';
import ParamControl from '../src/controls/ParamControl.svelte';
import NameField from '../src/programmer/NameField.svelte';
import PanelButton from '../src/panel/PanelButton.svelte';
import StartCard from '../src/web/StartCard.svelte';
import { resetBridge, setBackend } from '../src/bridge';
import { store } from '../src/store.svelte';
import { installGlobalInput, keyboard } from '../src/input';
import { webState, showStartCard } from '../src/web/webstate.svelte';
import { FakeBackend } from './fake-backend';
import { G } from '../src/params';

function control(props: Partial<Record<string, unknown>> = {}) {
  const onchange = vi.fn();
  const r = render(ParamControl, {
    props: { value: 10, display: '10', max: 31, label: 'OP1 Coarse', tip: 'OP1 Coarse (0 – 31)', onchange, ...props },
  });
  const el = r.getByRole((props.kind as string) === 'toggle' ? 'switch' : 'spinbutton');
  return { ...r, el, onchange };
}

async function typeEntry(el: HTMLElement, first: string, full: string, key: string) {
  el.focus();
  await fireEvent.keyDown(el, { key: first });
  await tick();
  await tick();
  const input = el.querySelector('input') as HTMLInputElement;
  expect(input).not.toBeNull();
  input.value = full;
  await fireEvent.input(input);
  await fireEvent.keyDown(input, { key });
  await tick();
}

describe('R-U1 programmer control (component)', () => {
  beforeEach(() => cleanup());

  it('R-U1 wheel +1, Shift+wheel +10', async () => {
    const { el, onchange } = control();
    await fireEvent.wheel(el, { deltaY: -100 });
    expect(onchange).toHaveBeenLastCalledWith(11, false);
    await fireEvent.wheel(el, { deltaY: -100, shiftKey: true });
    expect(onchange).toHaveBeenLastCalledWith(20, false);
  });

  it('R-U1 40 px drag = +10, Ctrl+drag 40 px = +2–3', async () => {
    const { el, onchange } = control();
    await fireEvent.pointerDown(el, { button: 0, clientX: 5, clientY: 100, pointerId: 1 });
    await fireEvent.pointerMove(el, { clientX: 5, clientY: 60, pointerId: 1 });
    expect(onchange).toHaveBeenLastCalledWith(20, false);
    await fireEvent.pointerUp(el, { pointerId: 1 });
    onchange.mockClear();
    await fireEvent.pointerDown(el, { button: 0, clientX: 5, clientY: 100, ctrlKey: true, pointerId: 2 });
    await fireEvent.pointerMove(el, { clientX: 5, clientY: 60, ctrlKey: true, pointerId: 2 });
    const v = onchange.mock.lastCall?.[0] as number;
    expect(v - 10).toBeGreaterThanOrEqual(2);
    expect(v - 10).toBeLessThanOrEqual(3);
  });

  it('R-U1 typing 150 clamps to the max; typing abc reverts; Esc reverts', async () => {
    const { el, onchange } = control();
    await typeEntry(el, '1', '150', 'Enter');
    expect(onchange).toHaveBeenLastCalledWith(31, true);
    onchange.mockClear();
    await typeEntry(el, '1', 'abc', 'Enter');
    expect(onchange).not.toHaveBeenCalled();
    expect(el.querySelector('input')).toBeNull();
    await typeEntry(el, '2', '25', 'Escape');
    expect(onchange).not.toHaveBeenCalled();
    expect(el.textContent?.trim()).toBe('10');
  });

  it('R-U1 double-click enters a number', async () => {
    const { el, onchange } = control();
    await fireEvent.dblClick(el);
    await tick();
    await tick();
    const input = el.querySelector('input') as HTMLInputElement;
    expect(input.value).toBe('10');
    input.value = '7';
    await fireEvent.input(input);
    await fireEvent.keyDown(input, { key: 'Enter' });
    expect(onchange).toHaveBeenLastCalledWith(7, true);
  });

  it('R-U1 a toggle: click toggles, wheel +1, typing 150 clamps to on', async () => {
    const { el, onchange } = control({ kind: 'toggle', value: 0, display: 'OFF', max: 1 });
    await fireEvent.click(el, { detail: 1 });
    expect(onchange).toHaveBeenLastCalledWith(1, true);
    await fireEvent.wheel(el, { deltaY: -1 });
    expect(onchange).toHaveBeenLastCalledWith(1, false);
    onchange.mockClear();
    await typeEntry(el, '1', '150', 'Enter');
    expect(onchange).toHaveBeenLastCalledWith(1, true);
  });

  it('R-U1 double-click on a toggle opens typed entry without changing the value', async () => {
    const { el, onchange, rerender } = control({ kind: 'toggle', value: 0, display: 'OFF', max: 1 });
    await fireEvent.click(el, { detail: 1 });
    expect(onchange).toHaveBeenLastCalledWith(1, true);
    // The parent re-renders with the new value before the second click of the double-click.
    await rerender({ value: 1, display: 'ON' });
    await fireEvent.click(el, { detail: 2 });
    await fireEvent.dblClick(el);
    await tick();
    await tick();
    expect(onchange).toHaveBeenLastCalledWith(0, true);
    const input = el.querySelector('input') as HTMLInputElement;
    expect(input.value).toBe('0');
  });

  it('R-U1 the value is always visible and the tooltip shows name and range without changing it', async () => {
    const { el, onchange } = control({ display: ' 7' });
    expect(el.textContent?.trim()).toBe('7');
    expect(el.getAttribute('title')).toBe('OP1 Coarse (0 – 31)');
    expect(el.getAttribute('aria-label')).toBe('OP1 Coarse');
    await fireEvent.mouseOver(el);
    await fireEvent.mouseEnter(el);
    expect(onchange).not.toHaveBeenCalled();
  });

  it('R-S5 no value is shown before the snapshot', () => {
    const { el } = control({ ready: false });
    expect(el.textContent?.trim()).toBe('');
  });
});

describe('F-4.5 programmer name field (component)', () => {
  beforeEach(() => {
    cleanup();
    resetBridge();
  });

  it('F-4.5 é😀 becomes ?? and the name is padded with spaces; paste truncates to 10', async () => {
    const fake = new FakeBackend({ sync: true });
    setBackend(fake, store);
    expect(store.ready).toBe(true);
    const r = render(NameField);
    const input = r.getByRole('textbox', { name: 'Voice Name' }) as HTMLInputElement;
    await fireEvent.focus(input);
    fake.sent.length = 0;
    input.value = 'é😀';
    await fireEvent.input(input);
    expect(input.value).toBe('??');
    const names = fake.of('setParam').filter((c) => c.i >= G.NAME && c.i < G.NAME + 10);
    const result = new Array(10).fill(0);
    for (let k = 0; k < 10; k++) result[k] = store.params[G.NAME + k];
    expect(String.fromCharCode(...result)).toBe('??        ');
    expect(names.length).toBeGreaterThan(0);
    input.value = 'A VERY LONG PASTED NAME';
    await fireEvent.input(input);
    expect(input.value).toBe('A VERY LON');
    const after = Array.from({ length: 10 }, (_, k) => store.params[G.NAME + k]);
    expect(String.fromCharCode(...after)).toBe('A VERY LON');
  });
});

describe('Front-panel buttons (component)', () => {
  beforeEach(() => {
    cleanup();
    resetBridge();
  });

  it('send button down on press and up on release, also when the release happens outside the window', async () => {
    const fake = new FakeBackend({ sync: true });
    setBackend(fake, store);
    const r = render(PanelButton, { props: { id: 42, x: 0, y: 0, w: 64, h: 40, label: 'YES' } });
    const b = r.getByRole('button', { name: 'YES' });
    await fireEvent.pointerDown(b, { button: 0, pointerId: 3 });
    await fireEvent.pointerDown(b, { button: 0, pointerId: 3 });
    expect(fake.of('button')).toEqual([{ t: 'button', id: 42, down: true }]);
    await fireEvent(b, new Event('lostpointercapture'));
    await fireEvent.pointerUp(b, { pointerId: 3 });
    expect(fake.of('button')).toEqual([
      { t: 'button', id: 42, down: true },
      { t: 'button', id: 42, down: false },
    ]);
  });
});

describe('§5.9 start card (component)', () => {
  beforeEach(() => {
    cleanup();
    resetBridge();
  });

  it('§5.9 the dismissing click and key reach no control and no keyboard note', async () => {
    const fake = new FakeBackend({ sync: true });
    setBackend(fake, store);
    installGlobalInput();
    keyboard.setPlay(true);
    const started = vi.fn();
    const p = showStartCard(started);
    expect(webState.startCard).toBe(true);
    fake.sent.length = 0;
    // A key dismisses the card and is swallowed (down and up).
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyA', key: 'a', bubbles: true, cancelable: true }));
    window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyA', key: 'a', bubbles: true, cancelable: true }));
    await p;
    expect(started).toHaveBeenCalledTimes(1);
    expect(webState.startCard).toBe(false);
    expect(fake.sent).toEqual([]);
    // The next key plays.
    window.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyA', key: 'a', bubbles: true, cancelable: true }));
    window.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyA', key: 'a', bubbles: true, cancelable: true }));
    expect(fake.of('noteOn')).toEqual([{ t: 'noteOn', note: 60, vel: 100 }]);

    // A click on the card dismisses it without reaching anything below.
    void showStartCard();
    const r = render(StartCard);
    const below = vi.fn();
    document.body.addEventListener('click', below);
    const card = r.getByTestId('start-card');
    await fireEvent.pointerDown(card);
    await fireEvent.click(card);
    expect(webState.startCard).toBe(false);
    expect(below).not.toHaveBeenCalled();
    document.body.removeEventListener('click', below);
  });
});

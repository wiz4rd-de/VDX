// Helpers for driving the real UI against the real WASM core (web build, e2e mode).
import { expect, type Locator, type Page } from '@playwright/test';
import { disableIndexedDb, installMidiDouble, midiListening, refuseInternalRate, type MidiMode } from './doubles';

export const PLAY_L1 = 'INTERNAL VOICE  ';
export const SPLASH_L1 = '      VDX       ';
export const SPLASH_L2 = ' FM SYNTHESIZER ';

export interface OpenOptions {
  midi?: MidiMode;
  refuseRate?: boolean;
  noIdb?: boolean;
}

/** Records every LCD / LED change with performance.now() (window.__lcdLog). */
async function installLcdRecorder(page: Page): Promise<void> {
  await page.addInitScript(() => {
    const w = window as unknown as { __lcdLog: [number, string, string, string, string][] };
    w.__lcdLog = [];
    const attach = (): void => {
      const lcd = document.querySelector<HTMLElement>('[data-testid=lcd]');
      const led = document.querySelector<HTMLElement>('[data-testid=led]');
      if (!lcd || !led) {
        requestAnimationFrame(attach);
        return;
      }
      let last = '';
      const rec = (): void => {
        const row: [number, string, string, string, string] = [
          performance.now(),
          lcd.dataset.l1 ?? '',
          lcd.dataset.l2 ?? '',
          lcd.dataset.lit ?? '',
          led.dataset.value ?? '',
        ];
        const key = row.slice(1).join('|');
        if (key !== last) {
          last = key;
          w.__lcdLog.push(row);
        }
      };
      rec();
      new MutationObserver(rec).observe(lcd, { attributes: true });
      new MutationObserver(rec).observe(led, { attributes: true });
    };
    attach();
  });
}

/** Opens the web build with the §10 test doubles; the start card is showing afterwards. */
export async function openApp(page: Page, opts: OpenOptions = {}) {
  await installMidiDouble(page, opts.midi ?? 'ok');
  if (opts.refuseRate) await refuseInternalRate(page);
  if (opts.noIdb) await disableIndexedDb(page);
  await installLcdRecorder(page);
  const response = await page.goto('/');
  await expect(page.getByTestId('start-card')).toBeVisible();
  return response;
}

export const lcd = (page: Page): Locator => page.getByTestId('lcd');

export async function lcdLines(page: Page): Promise<[string, string]> {
  const el = lcd(page);
  return [(await el.getAttribute('data-l1')) ?? '', (await el.getAttribute('data-l2')) ?? ''];
}

export async function expectLcd(page: Page, l1: string | RegExp | null, l2?: string | RegExp): Promise<void> {
  if (l1 !== null) await expect(lcd(page)).toHaveAttribute('data-l1', l1);
  if (l2 !== undefined) await expect(lcd(page)).toHaveAttribute('data-l2', l2);
}

/** Clicks the start card and waits until the splash is over (PLAY). */
export async function startApp(page: Page, opts: { waitMidi?: boolean } = {}): Promise<void> {
  await page.getByTestId('start-card').click();
  await expect(page.getByTestId('start-card')).toBeHidden();
  await expect(lcd(page)).toHaveAttribute('data-l1', SPLASH_L1, { timeout: 15_000 });
  await expect(lcd(page)).toHaveAttribute('data-l1', PLAY_L1, { timeout: 15_000 });
  if (opts.waitMidi !== false) await midiListening(page);
}

/** Opens the app and starts it. */
export async function launch(page: Page, opts: OpenOptions = {}): Promise<void> {
  await openApp(page, opts);
  await startApp(page, { waitMidi: opts.midi === undefined || opts.midi === 'ok' || opts.midi === 'nosysex' });
}

/** Presses and releases a front-panel button (1–32, 33–40, 'no', 'yes'). */
export async function press(page: Page, id: number | 'no' | 'yes'): Promise<void> {
  await page.getByTestId(`btn-${id}`).click();
}

/** Typed entry into a programmer control: focus, type, Enter (R-U1). */
export async function typeInto(page: Page, testid: string, text: string): Promise<void> {
  await page.getByTestId(testid).focus();
  await page.keyboard.type(text);
  await page.keyboard.press('Enter');
}

/** The displayed value string of a programmer control. */
export async function shown(page: Page, testid: string): Promise<string> {
  return ((await page.getByTestId(testid).textContent()) ?? '').trim();
}

export async function expectShown(page: Page, testid: string, value: string | RegExp): Promise<void> {
  await expect(page.getByTestId(testid)).toHaveText(value);
}

/** Analyser RMS (dBFS) of the worklet output (test build). */
export async function rms(page: Page): Promise<number> {
  return page.evaluate(() => (window as unknown as { __vdxTap?: { rmsDb(): number } }).__vdxTap?.rmsDb() ?? -999);
}

/** Highest analyser RMS seen while sampling every 20 ms for `ms`. */
export async function peakRms(page: Page, ms: number): Promise<number> {
  return page.evaluate(async (dur) => {
    const tap = (window as unknown as { __vdxTap?: { rmsDb(): number } }).__vdxTap;
    let peak = -999;
    const end = performance.now() + dur;
    while (performance.now() < end) {
      if (tap) peak = Math.max(peak, tap.rmsDb());
      await new Promise((r) => setTimeout(r, 20));
    }
    return peak;
  }, ms);
}

/** Waits until the analyser reports audio above `db`. */
export async function expectAudible(page: Page, db = -60): Promise<void> {
  await expect.poll(() => rms(page), { timeout: 3000, intervals: [20] }).toBeGreaterThan(db);
}

/** Opens the Settings overlay and selects the mocked MIDI output (web: none by default, §4.4). */
export async function selectMidiOutput(page: Page): Promise<void> {
  await page.getByTestId('open-settings').click();
  await page.getByTestId('midi-output').selectOption('mock-out');
  await page.getByTestId('settings-close').click();
}

/** Loads a file through "Load file…" (the browser file picker). */
export async function loadFile(page: Page, name: string, bytes: Buffer, trigger = 'bank-load'): Promise<void> {
  const chooser = page.waitForEvent('filechooser');
  await page.getByTestId(trigger).click();
  await (await chooser).setFiles({ name, mimeType: 'application/octet-stream', buffer: bytes });
}

/** Drops a file onto the window (F-12.2). */
export async function dropFile(page: Page, name: string, bytes: Buffer): Promise<void> {
  const dt = await page.evaluateHandle(
    ({ n, b64 }) => {
      const data = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      const t = new DataTransfer();
      t.items.add(new File([data], n, { type: 'application/octet-stream' }));
      return t;
    },
    { n: name, b64: bytes.toString('base64') },
  );
  await page.dispatchEvent('[data-testid=canvas]', 'dragover', { dataTransfer: dt });
  await page.dispatchEvent('[data-testid=canvas]', 'drop', { dataTransfer: dt });
}

/** Names of a bank's 32 rows in the bank manager. */
export async function bankNames(page: Page, bank: 0 | 1): Promise<string[]> {
  return page.evaluate(
    (b) =>
      Array.from({ length: 32 }, (_, i) =>
        (document.querySelector(`[data-testid=bank-row-${b}-${i + 1}] .nm`)?.textContent ?? '').trimEnd(),
      ),
    bank,
  );
}

/** Lit numbered buttons and lit / blinking mode buttons, from the DOM (§5.4). */
export async function lamps(page: Page): Promise<{ num: number[]; mode: number[]; blink: number[] }> {
  return page.evaluate(() => {
    const num: number[] = [];
    const mode: number[] = [];
    const blink: number[] = [];
    for (let n = 1; n <= 32; n++)
      if (document.querySelector(`[data-testid=btn-${n}]`)?.getAttribute('data-lit') === '1') num.push(n);
    for (let id = 33; id <= 40; id++) {
      const el = document.querySelector(`[data-testid=btn-${id}]`);
      if (el?.getAttribute('data-blink') === '1') blink.push(id);
      else if (el?.getAttribute('data-lit') === '1') mode.push(id);
    }
    return { num, mode, blink };
  });
}

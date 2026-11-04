// UI snapshot and update latency (R-S5, R-S6).
import { test, expect, type Page } from '@playwright/test';
import { launch, startApp, press, typeInto, expectShown, expectLcd, dropFile, PLAY_L1 } from './support/app';
import { bankWithNames, e2eNames } from './support/syx';

/** Everything the UI displays (values, LCD, LED, lamps, wheels, order, cartridge, bank names, settings). */
async function displayed(page: Page): Promise<Record<string, string>> {
  const main = await page.evaluate(() => {
    const out: Record<string, string> = {};
    for (const el of document.querySelectorAll<HTMLElement>('[data-testid]')) {
      const id = el.dataset.testid!;
      if (/^(param|func|global)-|^op-(freq|badge|switch)-|^lfo-(hz|wave-name)$/.test(id))
        out[id] = `${el.textContent?.trim()}|${el.getAttribute('aria-checked') ?? ''}`;
      else if (id === 'name-field') out[id] = (el as HTMLInputElement).value;
      else if (/^btn-/.test(id)) out[id] = `${el.dataset.lit}/${el.dataset.blink}`;
      else if (/^algo-thumb-/.test(id)) out[id] = el.getAttribute('aria-pressed') ?? '';
      else if (/^lfo-wave-\d$/.test(id)) out[id] = el.getAttribute('aria-checked') ?? '';
      else if (/^wheel-/.test(id)) out[id] = el.dataset.value ?? '';
      else if (id === 'slider-volume') out[id] = el.dataset.pos ?? '';
      else if (/^(eg-graph|peg-graph|ls-graph)/.test(id)) out[id] = el.innerHTML;
    }
    const lcd = document.querySelector<HTMLElement>('[data-testid=lcd]')!;
    out.lcd = [lcd.dataset.l1, lcd.dataset.l2, lcd.dataset.cursor, lcd.dataset.lit].join('|');
    const led = document.querySelector<HTMLElement>('[data-testid=led]')!;
    out.led = `${led.dataset.value}.${led.dataset.dot}`;
    out.cartridge = document.querySelector('[data-testid=cartridge-slot]')?.getAttribute('title') ?? '';
    out.opOrder = [...document.querySelectorAll<HTMLElement>('[data-op-panel]')].map((e) => e.dataset.opPanel).join('');
    return out;
  });
  // Bank names and header (bank manager) and the settings (Settings overlay).
  await page.getByTestId('cartridge-slot').click();
  main.bank = await page.getByTestId('bank-manager').innerText();
  await page.keyboard.press('Escape');
  await page.getByTestId('open-settings').click();
  main.settings = await page.evaluate(() =>
    [...document.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-testid=settings] input, [data-testid=settings] select')]
      .map((e) => (e instanceof HTMLInputElement && (e.type === 'checkbox' || e.type === 'radio') ? String(e.checked) : e.value))
      .join(','),
  );
  await page.keyboard.press('Escape');
  return main;
}

test('AC-S5.1 a non-default state is displayed identically after a page reload', async ({ page }) => {
  test.setTimeout(90_000);
  await launch(page);
  // Voice selection, programmer edits of voice, function and global values.
  await press(page, 4);
  await expectLcd(page, PLAY_L1, 'INT 4 BASS      ');
  await typeInto(page, 'param-121', '77'); // OP1 output level
  await typeInto(page, 'param-134', '12'); // algorithm
  await typeInto(page, 'param-137', '60'); // LFO speed
  await page.getByTestId('lfo-wave-3').click(); // SQU
  await typeInto(page, 'param-130', '70'); // pitch EG level 1
  await typeInto(page, 'param-65', '35'); // OP3 EG rate 3
  await typeInto(page, 'func-69', '40'); // portamento time
  await page.getByTestId('func-71-bit1').click(); // wheel → amp
  await typeInto(page, 'global-masterTune', '90');
  await typeInto(page, 'global-volume', '100');
  // (The operator mask is not part of the state, §4.1 / R-P7: it resets to all-on on load.)
  // Settings: operator order and engine.
  await page.getByTestId('open-settings').click();
  await page.getByTestId('setting-opOrder-1').check();
  await page.getByTestId('setting-engine-modern').check();
  await page.keyboard.press('Escape');
  // A cartridge.
  await dropFile(page, 's5 cartridge.syx', bankWithNames(e2eNames('S5')));
  await page.keyboard.press('Enter');
  await expect(page.getByTestId('cartridge-slot')).toHaveAttribute('title', 's5 cartridge');
  // Back to PLAY (panel mode and pages are not saved, §4.8).
  await press(page, 33);
  await expectLcd(page, PLAY_L1, 'INT 4 BASS      ');
  await expectShown(page, 'global-volume', '100');
  await page.waitForTimeout(1500);
  const before = await displayed(page);
  expect(before.opOrder).toBe('654321');
  expect(before.led).toBe('4.1');

  await page.reload();
  await startApp(page);
  const after = await displayed(page);
  expect(after).toEqual(before);
});

test('AC-S6.1 an injected CC1 64 moves the mod wheel within 100 ms', async ({ page }) => {
  await launch(page);
  await expect(page.getByTestId('wheel-mod')).toHaveAttribute('data-value', '0');
  const ms = await page.evaluate(
    () =>
      new Promise<number>((resolve, reject) => {
        const el = document.querySelector<HTMLElement>('[data-testid=wheel-mod]')!;
        const t0 = performance.now();
        const obs = new MutationObserver(() => {
          if (el.dataset.value === '64') {
            obs.disconnect();
            resolve(performance.now() - t0);
          }
        });
        obs.observe(el, { attributes: true });
        (window as unknown as { __midiInject(b: number[]): void }).__midiInject([0xb0, 1, 64]);
        setTimeout(() => reject(new Error('mod wheel did not move')), 2000);
      }),
  );
  expect(ms).toBeLessThan(100);
  await expect(page.getByTestId('wheel-mod')).toHaveAttribute('data-value', '64');
});

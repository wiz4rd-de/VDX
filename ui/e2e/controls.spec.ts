// Programmer controls, shortcuts and the name field against the real core
// (R-U1, R-U4, F-4.5, R-P8).
import { test, expect, type Page } from '@playwright/test';
import { launch, press, typeInto, expectShown, lcdLines, expectLcd, lcd, peakRms, rms } from './support/app';

async function wheel(page: Page, testid: string, shift = false): Promise<void> {
  await page.getByTestId(testid).hover();
  if (shift) await page.keyboard.down('Shift');
  await page.mouse.wheel(0, -100);
  if (shift) await page.keyboard.up('Shift');
}

async function drag(page: Page, testid: string, up: number, fine = false): Promise<void> {
  const b = (await page.getByTestId(testid).boundingBox())!;
  const x = b.x + b.width / 2;
  const y = b.y + b.height / 2;
  await page.mouse.move(x, y);
  if (fine) await page.keyboard.down('ControlOrMeta');
  await page.mouse.down();
  for (let k = 1; k <= 8; k++) await page.mouse.move(x, y - (up * k) / 8);
  await page.mouse.up();
  if (fine) await page.keyboard.up('ControlOrMeta');
}

async function typeAbc(page: Page, testid: string): Promise<void> {
  await page.getByTestId(testid).dblclick();
  const input = page.getByTestId(testid).locator('input');
  await expect(input).toBeFocused();
  await page.keyboard.type('abc');
  await page.keyboard.press('Enter');
  await expect(input).toHaveCount(0);
}

test.describe('AC-U1.1 programmer controls', () => {
  test('AC-U1.1 coarse: wheel +1, Shift+wheel +10, 40 px drag +10, Ctrl/Cmd+drag +2–3, typing 150 clamps, abc reverts', async ({ page }) => {
    await launch(page);
    const id = 'param-123'; // OP1 coarse; its value string is the ratio (R-P8)
    await expectShown(page, id, '1.00');
    await wheel(page, id);
    await expectShown(page, id, '2.00');
    await wheel(page, id, true);
    await expectShown(page, id, '12.00');
    await drag(page, id, 40);
    await expectShown(page, id, '22.00');
    await drag(page, id, 40, true);
    await expectShown(page, id, /^(24|25)\.00$/);
    await typeInto(page, id, '150');
    await expectShown(page, id, '31.00');
    await typeAbc(page, id);
    await expectShown(page, id, '31.00');
    await expectLcd(page, 'ALG 1 111111 OP1', 'F COARSE=31.00  ');
  });

  test('AC-U1.1 output level: wheel +1, Shift+wheel +10, 40 px drag +10, Ctrl/Cmd+drag +2–3, typing 150 clamps, abc reverts', async ({ page }) => {
    await launch(page);
    const id = 'param-100'; // OP2 output level (INIT VOICE: 0)
    await expectShown(page, id, '0');
    await wheel(page, id);
    await expectShown(page, id, '1');
    await wheel(page, id, true);
    await expectShown(page, id, '11');
    await drag(page, id, 40);
    await expectShown(page, id, '21');
    await drag(page, id, 40, true);
    await expectShown(page, id, /^(23|24)$/);
    await typeInto(page, id, '150');
    await expectShown(page, id, '99');
    await typeAbc(page, id);
    await expectShown(page, id, '99');
    await expectLcd(page, 'ALG 1 111111 OP2', 'OUTPUT LEVEL =99');
  });

  test('AC-U1.1 algorithm: wheel +1, Shift+wheel +10, 40 px drag +10, Ctrl/Cmd+drag +2–3, typing 150 clamps, abc reverts', async ({ page }) => {
    await launch(page);
    const id = 'param-134';
    await expectShown(page, id, '1');
    await wheel(page, id);
    await expectShown(page, id, '2');
    await wheel(page, id, true);
    await expectShown(page, id, '12');
    await drag(page, id, 40);
    await expectShown(page, id, '22');
    await drag(page, id, 40, true);
    await expectShown(page, id, /^(24|25)$/);
    await typeInto(page, id, '150');
    await expectShown(page, id, '32');
    await typeAbc(page, id);
    await expectShown(page, id, '32');
    await expectLcd(page, /^ALG32 111111/, 'ALGORITHM SELECT');
  });

  test('AC-U1.1 a toggle (glissando): wheel, Shift+wheel, drag and Ctrl/Cmd+drag turn it on; typing 150 clamps to ON; abc reverts', async ({ page }) => {
    await launch(page);
    const id = 'func-68';
    const off = async (): Promise<void> => {
      if ((await page.getByTestId(id).getAttribute('aria-checked')) === 'true') await page.getByTestId(id).click();
      await expectShown(page, id, 'OFF');
    };
    await expectShown(page, id, 'OFF');
    await wheel(page, id);
    await expectShown(page, id, 'ON');
    await off();
    await wheel(page, id, true);
    await expectShown(page, id, 'ON');
    await off();
    await drag(page, id, 40);
    await expectShown(page, id, 'ON');
    await off();
    await drag(page, id, 40, true);
    await expectShown(page, id, 'ON');
    await off();
    await typeInto(page, id, '150');
    await expectShown(page, id, 'ON');
    await typeAbc(page, id);
    await expectShown(page, id, 'ON');
    await expectLcd(page, 'FUNCTION CONTROL', 'GLISSANDO=ON    ');
  });
});

test('AC-U4.1 every shortcut acts and no note sounds', async ({ page }) => {
  await launch(page);
  await press(page, 35); // EDIT
  await press(page, 21); // EG RATE page shows OPn
  await expectLcd(page, 'ALG 1 111111 OP1');
  await page.mouse.move(5, 995);
  let peak = -999;
  const check = async (): Promise<void> => {
    peak = Math.max(peak, await peakRms(page, 120));
  };
  for (const op of [1, 2, 3, 4, 5, 6]) {
    await page.keyboard.press(`Alt+Digit${op}`);
    await expect(page.locator(`[data-op-panel="${op}"]`)).toBeFocused();
    await expectLcd(page, `ALG 1 111111 OP${op}`);
    await check();
  }
  await page.keyboard.press('Alt+Shift+Digit2');
  await expectLcd(page, /^ALG 1 101111/);
  await page.keyboard.press('Alt+Shift+Digit2');
  await expectLcd(page, /^ALG 1 111111/);
  await page.keyboard.press('Alt+Shift+Digit6');
  await expectLcd(page, /^ALG 1 111110/);
  await expect(page.getByTestId('op-switch-6')).toHaveAttribute('aria-checked', 'false');
  await check();
  await page.keyboard.press('Alt+KeyG');
  await expect(page.locator('[data-block="voice"]')).toBeFocused();
  await check();
  await page.keyboard.press('Alt+KeyB');
  await expect(page.getByTestId('bank-manager')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('bank-manager')).toHaveCount(0);
  await check();
  await page.keyboard.press('Alt+Comma');
  await expect(page.getByTestId('settings')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('settings')).toHaveCount(0);
  await check();
  expect(peak).toBeLessThan(-60);
  expect(await rms(page)).toBeLessThan(-60);
});

test.describe('AC-F4.1 programmer name field', () => {
  test('AC-F4.1 é😀 becomes ??, pasted text is truncated to 10, typing never plays notes', async ({ page }) => {
    await launch(page);
    const field = page.getByTestId('name-field');
    await field.click();
    await page.keyboard.press('ControlOrMeta+A');
    await page.keyboard.insertText('é😀');
    await expect(field).toHaveValue('??');
    await expectLcd(page, null, 'NAME= ??        ');
    await page.keyboard.press('ControlOrMeta+A');
    await page.keyboard.insertText('ABCDEFGHIJKLMNOP');
    await expect(field).toHaveValue('ABCDEFGHIJ');
    await expectLcd(page, null, 'NAME= ABCDEFGHIJ');
    // Physical keys typed into the field type characters and never play notes.
    await page.keyboard.press('ControlOrMeta+A');
    const typing = peakRms(page, 700);
    await page.keyboard.type('asdfgh', { delay: 40 });
    expect(await typing).toBeLessThan(-60);
    await expect(field).toHaveValue('asdfgh');
    await expectLcd(page, null, 'NAME= asdfgh    ');
    await page.keyboard.press('Enter');
    await expect(field).not.toBeFocused();
    expect((await lcdLines(page))[1]).toBe('NAME= asdfgh    ');
  });
});

// ---- R-P8: the programmer shows the core's strings, unclipped and equal to the LCD ----

/** The value field of the LCD page that shows `testid` (§5.3). */
function lcdValue(testid: string, l1: string, l2: string): string {
  if (testid === 'param-134') return l1.slice(3, 5).trim();
  if (!l2.includes('=')) return l2.trim();
  return l2.slice(l2.lastIndexOf('=') + 1).trim();
}

async function fieldFits(page: Page, testid: string): Promise<boolean> {
  return page.getByTestId(testid).evaluate((e) => e.scrollWidth <= e.clientWidth + 0.5);
}

const OP3 = (offset: number): string => `param-${63 + offset}`;

test.describe('AC-P8.1 programmer value strings (programmer part)', () => {
  test('AC-P8.1 every programmer field shows the LCD string at min and max, unclipped', async ({ page }) => {
    test.setTimeout(180_000);
    await launch(page);
    const voice = [
      ...Array.from({ length: 21 }, (_, k) => OP3(k)),
      ...Array.from({ length: 18 }, (_, k) => `param-${126 + k}`).filter((t) => t !== 'param-142'),
      'param-144',
    ];
    const func = [64, 65, 66, 67, 68, 69, 70, 72, 74, 76].map((n) => `func-${n}`);
    const problems: string[] = [];
    for (const id of [...voice, ...func]) {
      // Max first (it may already be there and then sends nothing), then min and
      // max again: both are changes. Each starts from PLAY, because a programmer
      // change shows that parameter's page only from PLAY / FUNCTION (voice) or
      // from outside FUNCTION (function parameters), F-3.
      await typeInto(page, id, '999');
      for (const typed of ['-99', '999']) {
        await press(page, 33);
        await typeInto(page, id, typed);
        await page.waitForTimeout(80);
        const [l1, l2] = await lcdLines(page);
        const want = lcdValue(id, l1, l2);
        const got = (await page.getByTestId(id).textContent())?.trim() ?? '';
        if (got !== want) problems.push(`${id} typed ${typed}: programmer "${got}" vs LCD "${l2}"`);
        if (!(await fieldFits(page, id))) problems.push(`${id} typed ${typed}: "${got}" is clipped`);
      }
    }
    expect(problems).toEqual([]);
  });

  test('AC-P8.1 dependent value strings follow their siblings (coarse/fine ↔ mode, porta mode ↔ mono, bend range ↔ step)', async ({ page }) => {
    await launch(page);
    const problems: string[] = [];
    const compareOnPage = async (id: string, button: number): Promise<void> => {
      await press(page, button);
      await page.waitForTimeout(80);
      const [l1, l2] = await lcdLines(page);
      const got = (await page.getByTestId(id).textContent())?.trim() ?? '';
      if (got !== lcdValue(id, l1, l2)) problems.push(`${id}: programmer "${got}" vs LCD page ${button} "${l2}"`);
    };
    // OP3: ratio coarse 2, fine 50, then fixed mode (from PLAY, so EDIT shows page 17, F-3).
    await typeInto(page, OP3(18), '2');
    await typeInto(page, OP3(19), '50');
    await press(page, 33);
    await typeInto(page, OP3(17), '1');
    await expectLcd(page, /OP3$/, 'FIXED FREQ.(Hz) ');
    await compareOnPage(OP3(18), 18);
    await compareOnPage(OP3(19), 19);
    // Fine changes the coarse string too (both show the frequency).
    await typeInto(page, OP3(19), '99');
    await compareOnPage(OP3(18), 18);
    // Poly → mono changes the porta-mode string; a bend step shows the range as 12.
    await press(page, 33);
    await typeInto(page, 'func-64', '1');
    await expectLcd(page, 'FUNCTION CONTROL', 'MONO MODE       ');
    await compareOnPage('func-67', 5);
    await typeInto(page, 'func-66', '3');
    await compareOnPage('func-65', 3);
    expect(problems).toEqual([]);
    await expect(lcd(page)).toHaveAttribute('data-lit', '1');
  });
});

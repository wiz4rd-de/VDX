// Lamps (§5.4) for every panel mode and every numbered button — the lamp part
// of AC-P1.1 (the page/parameter part is the core golden).
import { test, expect, type Page } from '@playwright/test';
import { launch, press, lamps, lcdLines, expectLcd, PLAY_L1 } from './support/app';

const pad2 = (n: number): string => String(n).padStart(2, ' ');

async function expectLamps(page: Page, num: number[], mode: number[], blink: number[] = []): Promise<void> {
  await expect.poll(() => lamps(page), { timeout: 3000 }).toEqual({ num, mode, blink });
}

test.describe('AC-P1.1 lamps', () => {
  test('AC-P1.1 PLAY: each button 1–32 lights its slot; MEMORY SELECT INTERNAL is lit; STORE / protect dialogs light their button', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    await expectLamps(page, [1], [33]);
    for (let n = 1; n <= 32; n++) {
      await press(page, n);
      await expectLamps(page, [n], [33]);
      expect((await lcdLines(page))[1].slice(0, 5)).toBe(`INT${pad2(n)}`);
    }
    // Store dialog: STORE lit while pending.
    await press(page, 37);
    await expectLcd(page, PLAY_L1, ' MEMORY STORE   ');
    await expectLamps(page, [32], [33, 37]);
    await press(page, 37); // STORE again cancels
    await expectLamps(page, [32], [33]);
    // Memory protect dialogs light their own button.
    await press(page, 38);
    await expectLcd(page, ' MEMORY PROTECT ');
    expect((await lamps(page)).mode).toContain(38);
    await press(page, 39);
    await expectLcd(page, ' MEMORY PROTECT ', /^ CARTRIDGE/);
    expect((await lamps(page)).mode).toContain(39);
    expect((await lamps(page)).mode).not.toContain(38);
  });

  test('AC-P1.1 EDIT and EDIT_NAME: EDIT/COMPARE and the current page are lit; COMPARE blinks; EG copy lights STORE', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    await press(page, 35);
    await expectLamps(page, [7], [35]); // last edit page 7 after the splash (F-1.5)
    let pageNow = 7;
    for (let n = 1; n <= 31; n++) {
      await press(page, n);
      if (n >= 7) pageNow = n; // 1–6 toggle an operator; the page stays
      await expectLamps(page, [pageNow], [35]);
    }
    // Buttons 1–6 were each pressed once: all operators are off now; turn them back on.
    await expectLcd(page, /^ALG 1 000000/);
    for (let n = 1; n <= 6; n++) await press(page, n);
    await expectLcd(page, /^ALG 1 111111/);
    await expectLamps(page, [31], [35]);
    // EG-copy dialog (STORE in EDIT).
    await press(page, 21);
    await press(page, 37);
    await expectLcd(page, ' EG COPY        ');
    await expectLamps(page, [21], [35, 37]);
    await press(page, 37); // cancel
    await expectLamps(page, [21], [35]);
    // 32 → EDIT_NAME: every numbered button types a character; page 32 stays lit.
    await press(page, 32);
    await expectLamps(page, [32], [35]);
    for (let n = 1; n <= 32; n++) {
      await press(page, n);
      await expectLamps(page, [32], [35]);
    }
    // Characters 1–10 take 1…0; from then on character 10 is overwritten (F-4): the last is 'V'.
    await expectLcd(page, null, 'NAME= 123456789V');
    await press(page, 36); // FUNCTION leaves naming: EDIT, VOICE NAME page
    await expectLamps(page, [32], [35]);
    // COMPARE (EDITED): the EDIT/COMPARE lamp blinks.
    await press(page, 7);
    await press(page, 35);
    await expectLcd(page, PLAY_L1);
    await expectLamps(page, [7], [], [35]); // still EDIT page 7; the EDIT/COMPARE lamp blinks (§5.4)
    await press(page, 35);
    await expectLamps(page, [7], [35]);
  });

  test('AC-P1.1 FUNCTION: FUNCTION and the current page are lit; 12 and 13 have no effect', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    await press(page, 36);
    await expectLamps(page, [1], [36]); // last function page 1 after the splash (F-1.5)
    let pageNow = 1;
    for (let n = 1; n <= 32; n++) {
      await press(page, n);
      if (n !== 12 && n !== 13) pageNow = n;
      await expectLamps(page, [pageNow], [36]);
    }
  });
});

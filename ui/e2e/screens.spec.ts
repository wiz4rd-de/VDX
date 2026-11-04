// AC-SC1: screenshots at 50 / 100 / 200 % in PLAY, EDIT (page 21), FUNCTION
// (page 17), the bank manager and Settings, compared with committed baselines
// (≤ 0.1 % differing pixels, playwright.config.ts). Chromium only; baselines
// exist per platform (darwin locally, linux in the Playwright Docker image).
import { test, expect, type Page } from '@playwright/test';
import { launch, press, expectLcd, PLAY_L1 } from './support/app';

const SCALES = [
  { pct: 50, viewport: { width: 640, height: 500 } },
  { pct: 100, viewport: { width: 1280, height: 1000 } },
  { pct: 200, viewport: { width: 2560, height: 2000 } },
] as const;

const STATES: { name: string; go: (page: Page) => Promise<void> }[] = [
  { name: 'play', go: async (page) => expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE') },
  {
    name: 'edit-21',
    go: async (page) => {
      await press(page, 35);
      await press(page, 21);
      await expectLcd(page, 'ALG 1 111111 OP1', 'EG  RATE  1  =99');
    },
  },
  {
    name: 'function-17',
    go: async (page) => {
      await press(page, 36);
      await press(page, 17);
      await expectLcd(page, 'FUNCTION CONTROL', 'WHEEL  RANGE=99 ');
    },
  },
  {
    name: 'bank-manager',
    go: async (page) => {
      await page.getByTestId('cartridge-slot').click();
      await expect(page.getByTestId('bank-manager')).toBeVisible();
    },
  },
  {
    name: 'settings',
    go: async (page) => {
      await page.getByTestId('open-settings').click();
      await expect(page.getByTestId('settings')).toBeVisible();
    },
  },
];

for (const scale of SCALES) {
  test.describe(`AC-SC1 screenshots at ${scale.pct} %`, () => {
    test.use({ viewport: scale.viewport });
    for (const state of STATES) {
      test(`AC-SC1 ${state.name} at ${scale.pct} %`, async ({ page, browserName }) => {
        test.skip(browserName !== 'chromium', 'AC-SC1 baselines are Chromium screenshots');
        await launch(page);
        await expect(page.getByTestId('canvas')).toHaveAttribute('data-scale', String(scale.pct / 100));
        await state.go(page);
        // Neutral pointer and no focus ring; fonts loaded; let the 400 ms blink clock settle.
        await page.mouse.move(0, 0);
        await page.evaluate(async () => {
          (document.activeElement as HTMLElement | null)?.blur();
          await document.fonts.ready;
        });
        await page.waitForTimeout(200);
        await expect(page).toHaveScreenshot(`${state.name}-${scale.pct}.png`, { fullPage: true });
      });
    }
  });
}

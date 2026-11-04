// Web page states, start-up, audio and persistence (§5.9, F-1, F-15.3).
import { test, expect } from '@playwright/test';
import {
  openApp,
  launch,
  startApp,
  expectLcd,
  expectAudible,
  peakRms,
  rms,
  typeInto,
  expectShown,
  press,
  lcd,
  PLAY_L1,
  SPLASH_L1,
  SPLASH_L2,
} from './support/app';
import { midiInject, midiListening } from './support/doubles';

test.describe('AC-B2 web build served on localhost:5173', () => {
  test('AC-B2 served on http://localhost:5173 with COOP/COEP headers, cross-origin isolated', async ({ page }) => {
    const res = await openApp(page);
    expect(page.url()).toBe('http://localhost:5173/');
    const h = res!.headers();
    expect(h['cross-origin-opener-policy']).toBe('same-origin');
    expect(h['cross-origin-embedder-policy']).toBe('require-corp');
    expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);
  });

  test('AC-B2 a computer-keyboard key gives analyser RMS > −60 dBFS', async ({ page }) => {
    await launch(page);
    expect(await rms(page)).toBeLessThan(-60);
    await page.keyboard.down('KeyA');
    await expectAudible(page);
    await page.keyboard.up('KeyA');
  });

  test('AC-B2 a mocked Web MIDI note gives analyser RMS > −60 dBFS (Chromium)', async ({ page, browserName }) => {
    test.skip(browserName !== 'chromium', 'Web MIDI is a Chromium criterion (§9.2)');
    await launch(page);
    await midiInject(page, [0x90, 60, 100]);
    await expectAudible(page);
    await midiInject(page, [0x80, 60, 0]);
  });
});

test('AC-E1.3 an AudioContext that refuses 49,096 Hz still gives a non-silent key', async ({ page }) => {
  await launch(page, { refuseRate: true });
  const rate = await page.evaluate(() => (window as unknown as { __vdxTap?: { sampleRate: number } }).__vdxTap?.sampleRate);
  expect(rate).toBeDefined();
  expect(rate).not.toBe(49096);
  await expect(page.getByTestId('web-banner')).toHaveCount(0);
  await page.keyboard.down('KeyA');
  await expectAudible(page);
  await page.keyboard.up('KeyA');
});

test.describe('AC-SC2 web page states', () => {
  test('AC-SC2 before the gesture the start card shows over the full UI, LCD unlit, LED blank', async ({ page }) => {
    await openApp(page);
    await expect(page.getByTestId('start-card')).toHaveText('Click or press any key to start VDX');
    await expect(page.getByTestId('front-panel')).toBeVisible();
    await expect(page.getByTestId('programmer')).toBeVisible();
    await expect(lcd(page)).toHaveAttribute('data-lit', '0');
    await expect(page.getByTestId('led')).toHaveAttribute('data-value', '0');
  });

  test('AC-SC2 served without COOP/COEP the isolation error screen shows', async ({ page }) => {
    await page.goto('http://localhost:5174/');
    await expect(page.getByTestId('fatal-error')).toHaveText(
      'VDX needs to be opened from http://localhost:5173 (cross-origin isolation is off).',
    );
    await expect(page.getByTestId('front-panel')).toHaveCount(0);
  });

  test('AC-SC2 requestMIDIAccess rejected: Settings shows "MIDI not available"', async ({ page }) => {
    await launch(page, { midi: 'reject' });
    await page.getByTestId('open-settings').click();
    await expect(page.getByTestId('midi-unavailable')).toHaveText('MIDI not available — use the computer keyboard.');
  });

  test('AC-SC2 MIDI granted without SysEx: Settings shows "SysEx not permitted" and Send bank is disabled', async ({ page }) => {
    await launch(page, { midi: 'nosysex' });
    await page.getByTestId('open-settings').click();
    await expect(page.getByTestId('midi-nosysex')).toHaveText('SysEx not permitted — dumps disabled.');
    await page.getByTestId('midi-output').selectOption('mock-out');
    await page.getByTestId('settings-close').click();
    await page.getByTestId('cartridge-slot').click();
    await expect(page.getByTestId('bank-send')).toBeDisabled();
    await expect(page.getByTestId('bank-request')).toBeDisabled();
    await expect(page.getByTestId('bank-send').locator('..')).toHaveAttribute('title', 'SysEx not permitted');
  });
});

test.describe('AC-F1.1 web start-up', () => {
  test('AC-F1.1 the start card shows first and its dismissing click activates nothing', async ({ page }) => {
    await openApp(page);
    // The card covers the window: click exactly where algorithm thumbnail 8 is.
    const thumb = await page.getByTestId('algo-thumb-8').boundingBox();
    await page.mouse.click(thumb!.x + thumb!.width / 2, thumb!.y + thumb!.height / 2);
    await expect(page.getByTestId('start-card')).toBeHidden();
    await expect(lcd(page)).toHaveAttribute('data-l1', PLAY_L1, { timeout: 5000 });
    await expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE');
    await expectShown(page, 'param-134', '1');
    await expect(page.getByTestId('led')).toHaveAttribute('data-dot', '0');
  });

  test('AC-F1.1 a dismissing key is not passed on to the keyboard (no note)', async ({ page }) => {
    await openApp(page);
    await page.keyboard.down('KeyA');
    await expect(page.getByTestId('start-card')).toBeHidden();
    const peak = await peakRms(page, 600);
    await page.keyboard.up('KeyA');
    expect(peak).toBeLessThan(-60);
  });

  test('AC-F1.1 the splash shows for 1.5 s ± 50 ms, then PLAY', async ({ page }) => {
    await openApp(page);
    await startApp(page, { waitMidi: false });
    await expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE');
    const log = await page.evaluate(() => (window as unknown as { __lcdLog: [number, string, string, string, string][] }).__lcdLog);
    const iSplash = log.findIndex((r) => r[1] === SPLASH_L1 && r[2] === SPLASH_L2 && r[3] === '1');
    expect(iSplash).toBeGreaterThanOrEqual(0);
    const iPlay = log.findIndex((r, i) => i > iSplash && r[1] === PLAY_L1);
    expect(iPlay).toBeGreaterThan(iSplash);
    const ms = log[iPlay][0] - log[iSplash][0];
    expect(ms).toBeGreaterThanOrEqual(1450);
    expect(ms).toBeLessThanOrEqual(1550);
    // Nothing else is shown between the card and the splash; the LED is blank during the splash.
    for (const r of log.slice(0, iSplash)) expect(r[3]).toBe('0');
    for (const r of log.slice(iSplash, iPlay)) expect(r[4]).toBe('0');
    expect(log[iPlay][4]).toBe('1');
  });

  test('AC-F1.1 a panel input during the splash is ignored', async ({ page }) => {
    await openApp(page);
    await page.getByTestId('start-card').click();
    await expectLcd(page, SPLASH_L1);
    await press(page, 7);
    await press(page, 'yes');
    await expect(lcd(page)).toHaveAttribute('data-l1', PLAY_L1, { timeout: 5000 });
    await expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE');
    await expect(page.getByTestId('led')).toHaveAttribute('data-value', '1');
  });

  test('AC-F1.1 a MIDI note during the splash sounds', async ({ page }) => {
    await openApp(page);
    await page.getByTestId('start-card').click();
    await expectLcd(page, SPLASH_L1);
    await midiListening(page);
    await midiInject(page, [0x90, 60, 100]);
    await expectAudible(page);
    await expectLcd(page, SPLASH_L1);
    await midiInject(page, [0x80, 60, 0]);
  });
});

test.describe('AC-F15.2 web persistence', () => {
  test('AC-F15.2 edit, reload: the same state returns', async ({ page }) => {
    await launch(page);
    await typeInto(page, 'param-134', '7');
    await expectShown(page, 'param-134', '7');
    await typeInto(page, 'param-100', '42'); // OP2 output level
    await expectShown(page, 'param-100', '42');
    await press(page, 33); // back to PLAY
    await expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE');
    await expect(lcd(page)).toHaveAttribute('data-cursor', '1,6');
    await page.waitForTimeout(1500); // saved 1 s after the last change
    await page.reload();
    await expect(page.getByTestId('start-card')).toBeVisible();
    await startApp(page);
    await expectShown(page, 'param-134', '7');
    await expectShown(page, 'param-100', '42');
    await expectLcd(page, PLAY_L1, 'INT 1 INIT VOICE');
    await expect(lcd(page)).toHaveAttribute('data-cursor', '1,6');
    await expect(page.getByTestId('led')).toHaveAttribute('data-dot', '1');
    await expect(page.getByTestId('web-banner')).toHaveCount(0);
  });

  test('AC-F15.2 with IndexedDB disabled the banner shows and VDX keeps running', async ({ page }) => {
    await openApp(page, { noIdb: true });
    await expect(page.getByTestId('web-banner')).toContainText('Changes will not be saved in this browser.');
    await startApp(page);
    await expect(page.getByTestId('web-banner')).toContainText('Changes will not be saved in this browser.');
    await typeInto(page, 'param-134', '9');
    await expectShown(page, 'param-134', '9');
  });
});

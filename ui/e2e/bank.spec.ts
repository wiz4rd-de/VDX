// Bank manager (§5.7, F-12) against the real core.
import { test, expect, type Page } from '@playwright/test';
import { launch, loadFile, dropFile, bankNames, expectLcd, lcd, selectMidiOutput, PLAY_L1 } from './support/app';
import { midiSent } from './support/doubles';
import { bankWithNames, defaultBank, e2eNames, headerless, voiceDump, wav } from './support/syx';

const DEFAULT_NAMES = ['INIT VOICE', 'E.PIANO', 'BELL', 'BASS', 'BRASS', 'ORGAN', 'MARIMBA', 'STRINGS', 'PAD'];
const defaultInternal = (): string[] => Array.from({ length: 32 }, (_, i) => DEFAULT_NAMES[i] ?? 'INIT VOICE');

async function openBank(page: Page): Promise<void> {
  await page.getByTestId('cartridge-slot').click();
  await expect(page.getByTestId('bank-manager')).toBeVisible();
}

async function chooseImport(page: Page, target: 'crt' | 'int'): Promise<void> {
  await expect(page.getByTestId('import-choice')).toBeVisible();
  await page.getByTestId(target === 'crt' ? 'import-crt' : 'import-int').click();
  await expect(page.getByTestId('import-choice')).toBeHidden();
}

test.describe('AC-F12.1 bank manager', () => {
  test('AC-F12.1 a 163-byte voice file goes to the edit buffer as a voice load: EDITED, voice source unchanged', async ({ page }) => {
    await launch(page);
    await openBank(page);
    await loadFile(page, 'e2e voice.syx', voiceDump('E2E VOICE', 5));
    await expect(page.getByTestId('bank-edit-buffer')).toContainText('E2E VOICE');
    await expectLcd(page, PLAY_L1, 'INT 1 E2E VOICE ');
    await expect(lcd(page)).toHaveAttribute('data-cursor', '1,6');
    await expect(page.getByTestId('led')).toHaveAttribute('data-dot', '1');
    await expect(page.getByTestId('param-134')).toHaveText('5');
    expect(await bankNames(page, 0)).toEqual(defaultInternal());
  });

  test('AC-F12.1 a 4104-byte bank: insert as cartridge; load into internal with protect on (refused) and off', async ({ page }) => {
    await launch(page);
    await openBank(page);
    await expect(page.getByTestId('bank-name-1')).toHaveText('NO CARTRIDGE');
    const bank = bankWithNames(e2eNames('CRT'));
    // Insert as cartridge: name = file stem, changed flag clear.
    await loadFile(page, 'e2e-bank.syx', bank);
    await chooseImport(page, 'crt');
    await expect(page.getByTestId('bank-name-1')).toHaveText('e2e-bank');
    expect(await bankNames(page, 1)).toEqual(e2eNames('CRT'));
    await expect(page.getByTestId('cartridge-slot')).toHaveAttribute('title', 'e2e-bank');
    // Into internal while internal protect is on (the default): refused.
    const intBank = bankWithNames(e2eNames('INT'));
    await expect(page.getByTestId('bank-protect-0')).toHaveAttribute('aria-checked', 'true');
    await loadFile(page, 'int-bank.syx', intBank);
    await chooseImport(page, 'int');
    await expect(page.getByTestId('bank-status')).toHaveText('Internal memory is protected.');
    await expectLcd(page, null, 'MEMORY PROTECTED');
    expect(await bankNames(page, 0)).toEqual(defaultInternal());
    // Protect off: loads.
    await page.getByTestId('bank-protect-0').click();
    await expect(page.getByTestId('bank-protect-0')).toHaveAttribute('aria-checked', 'false');
    await loadFile(page, 'int-bank.syx', intBank);
    await chooseImport(page, 'int');
    await expect.poll(() => bankNames(page, 0)).toEqual(e2eNames('INT'));
    // The 4 s notice ends.
    await expect(page.getByTestId('bank-status')).toHaveText('', { timeout: 6000 });
  });

  test('AC-F12.1 a headerless 4096-byte bank is accepted', async ({ page }) => {
    await launch(page);
    await openBank(page);
    await loadFile(page, 'raw bank.syx', headerless(bankWithNames(e2eNames('RAW'))));
    await chooseImport(page, 'crt');
    await expect(page.getByTestId('bank-name-1')).toHaveText('raw bank');
    expect(await bankNames(page, 1)).toEqual(e2eNames('RAW'));
  });

  test('AC-F12.1 a WAV is rejected with the inline notice and FORMAT CONFLICT!', async ({ page }) => {
    await launch(page);
    await openBank(page);
    await loadFile(page, 'drums.wav', wav());
    await expect(page.getByTestId('bank-status')).toHaveText('Not a voice or bank file: drums.wav');
    await expectLcd(page, null, 'FORMAT CONFLICT!');
    await expect(page.getByTestId('import-choice')).toHaveCount(0);
    expect(await bankNames(page, 0)).toEqual(defaultInternal());
    await expect(page.getByTestId('bank-status')).toHaveText('', { timeout: 6000 });
  });

  test('AC-F12.1 a file dropped anywhere on the window: the bank choice preselects "Insert as cartridge"; Enter confirms', async ({ page }) => {
    await launch(page);
    await dropFile(page, 'dropped.syx', bankWithNames(e2eNames('DRP')));
    await expect(page.getByTestId('import-choice')).toBeVisible();
    await expect(page.getByTestId('import-crt')).toBeFocused();
    await page.keyboard.press('Enter');
    await expect(page.getByTestId('import-choice')).toBeHidden();
    await expect(page.getByTestId('cartridge-slot')).toHaveAttribute('title', 'dropped');
    // A dropped voice goes to the edit buffer.
    await dropFile(page, 'v.syx', voiceDump('DROP VOICE'));
    await expectLcd(page, PLAY_L1, 'INT 1 DROP VOICE');
  });

  test('AC-F12.1 New cartridge, and New / Eject of a changed cartridge ask first', async ({ page }) => {
    await launch(page);
    await openBank(page);
    // No cartridge: New inserts UNTITLED at once, Eject is disabled.
    await expect(page.getByTestId('bank-eject')).toBeDisabled();
    await page.getByTestId('bank-new').click();
    await expect(page.getByTestId('bank-name-1')).toHaveText('UNTITLED');
    expect(await bankNames(page, 1)).toEqual(Array(32).fill('INIT VOICE'));
    // Make it changed: protect off, drag INT 2 onto CRT 1.
    await page.getByTestId('bank-protect-1').click();
    await expect(page.getByTestId('bank-protect-1')).toHaveAttribute('aria-checked', 'false');
    await page.getByTestId('bank-row-0-2').dragTo(page.getByTestId('bank-row-1-1'));
    await expect(page.getByTestId('bank-name-1')).toHaveText('UNTITLED •');
    expect((await bankNames(page, 1))[0]).toBe('E.PIANO');
    // New: confirmation; Cancel keeps it.
    await page.getByTestId('bank-new').click();
    await expect(page.getByTestId('bank-status')).toContainText('Replace the current cartridge? Unsaved changes will be lost.');
    await page.getByTestId('bank-confirm-cancel').click();
    expect((await bankNames(page, 1))[0]).toBe('E.PIANO');
    // New: Replace inserts a fresh UNTITLED.
    await page.getByTestId('bank-new').click();
    await page.getByTestId('bank-confirm-replace').click();
    await expect(page.getByTestId('bank-name-1')).toHaveText('UNTITLED');
    await expect.poll(async () => (await bankNames(page, 1))[0]).toBe('INIT VOICE');
    // Eject (changed): the same confirmation, then NO CARTRIDGE.
    await page.getByTestId('bank-row-0-3').dragTo(page.getByTestId('bank-row-1-1'));
    await expect(page.getByTestId('bank-name-1')).toHaveText('UNTITLED •');
    await page.getByTestId('bank-eject').click();
    await expect(page.getByTestId('bank-status')).toContainText('Replace the current cartridge? Unsaved changes will be lost.');
    await page.getByTestId('bank-confirm-replace').click();
    await expect(page.getByTestId('bank-name-1')).toHaveText('NO CARTRIDGE');
    await expect(page.getByTestId('bank-eject')).toBeDisabled();
    await expect(page.getByTestId('cartridge-slot')).toHaveAttribute('title', 'NO CARTRIDGE');
  });

  test('AC-F12.1 Export writes downloads with sanitised names; exporting the cartridge clears its changed flag', async ({ page }) => {
    await launch(page);
    await openBank(page);
    // Internal bank.
    let dl = page.waitForEvent('download');
    await page.getByTestId('bank-export-0').click();
    let d = await dl;
    expect(d.suggestedFilename()).toBe('INTERNAL.syx');
    const intBytes = await readDownload(d);
    expect(intBytes.length).toBe(4104);
    expect(Buffer.compare(intBytes, defaultBank())).toBe(0); // unchanged import/export is byte-identical (R-D3)
    // Cartridge with a name that needs sanitising, made changed by a drop.
    await loadFile(page, 'My Bank (v2)!.syx', bankWithNames(e2eNames('EXP')));
    await chooseImport(page, 'crt');
    await page.getByTestId('bank-protect-1').click();
    await page.getByTestId('bank-row-0-4').dragTo(page.getByTestId('bank-row-1-2'));
    await expect(page.getByTestId('bank-name-1')).toHaveText('My Bank (v2)! •');
    dl = page.waitForEvent('download');
    await page.getByTestId('bank-export-1').click();
    d = await dl;
    expect(d.suggestedFilename()).toBe('My Bank _v2__.syx');
    const crtBytes = await readDownload(d);
    expect(crtBytes.length).toBe(4104);
    expect([...crtBytes.subarray(0, 6)]).toEqual([0xf0, 0x43, 0x00, 0x09, 0x20, 0x00]);
    await expect(page.getByTestId('bank-name-1')).toHaveText('My Bank (v2)!');
    // Voice (163 bytes).
    await loadFile(page, 'x.syx', voiceDump('LEAD/2 *'));
    await expect(page.getByTestId('bank-edit-buffer')).toContainText('LEAD/2 *');
    dl = page.waitForEvent('download');
    await page.getByTestId('bank-export-voice').click();
    d = await dl;
    expect(d.suggestedFilename()).toBe('LEAD_2 _.syx');
    expect((await readDownload(d)).length).toBe(163);
  });
});

async function readDownload(d: import('@playwright/test').Download): Promise<Buffer> {
  const stream = await d.createReadStream();
  const chunks: Buffer[] = [];
  for await (const c of stream) chunks.push(Buffer.from(c as Buffer));
  return Buffer.concat(chunks);
}

test('AC-F12.2 mocked MIDI with no reply: Request dump shows "Waiting for bank…", then "No reply." after 10 s', async ({ page }) => {
  test.setTimeout(40_000);
  await launch(page);
  await selectMidiOutput(page);
  await openBank(page);
  await expect(page.getByTestId('bank-request')).toBeEnabled();
  await page.getByTestId('bank-request').click();
  const t0 = Date.now();
  await expect(page.getByTestId('bank-status')).toHaveText('Waiting for bank…');
  await expect.poll(() => midiSent(page)).toContainEqual([0xf0, 0x43, 0x20, 0x09, 0xf7]);
  await page.waitForTimeout(9000);
  await expect(page.getByTestId('bank-status')).toHaveText('Waiting for bank…');
  await expect(page.getByTestId('bank-status')).toHaveText('No reply.', { timeout: 3000 });
  expect(Date.now() - t0).toBeGreaterThanOrEqual(9900);
});

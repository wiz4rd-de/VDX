// Accessibility (R-U5): Tab order, visible focus ring, ARIA names (axe-core).
import { test, expect, type Page } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';
import { launch } from './support/app';

const range = (a: number, b: number): number[] => Array.from({ length: b - a + 1 }, (_, i) => a + i);
const opParam = (op: number, offset: number): number => (6 - op) * 21 + offset;

/** R-U5 order: front panel left to right, then programmer blocks left to right, top to bottom. */
function expectedOrder(opOrder: number[]): string[] {
  const front = [
    'wheel-bend', 'wheel-mod', 'slider-volume', 'slider-data', 'btn-no', 'btn-yes',
    ...range(33, 40).map((i) => `btn-${i}`), // mode buttons, left column then right column
    ...range(1, 32).map((i) => `btn-${i}`),
    'cartridge-slot', 'open-settings',
  ];
  const op = (n: number): string[] => [
    `op-switch-${n}`,
    ...[17, 20, 18, 19, 16, 15, 14, 13, 0, 1, 2, 3, 4, 5, 6, 7, 11, 9, 8, 10, 12].map((o) => `param-${opParam(n, o)}`),
  ];
  const programmer = [
    ...range(1, 32).map((i) => `algo-thumb-${i}`),
    'param-134',
    'voice-menu-button', 'name-field', 'param-135', 'param-136', 'param-144', 'param-143',
    ...range(0, 5).map((k) => `lfo-wave-${k}`), ...range(137, 141).map((i) => `param-${i}`),
    ...range(126, 133).map((i) => `param-${i}`),
    ...opOrder.flatMap(op),
    'func-64', 'func-65', 'func-66', 'func-67', 'func-68', 'func-69', 'global-masterTune', 'global-volume',
    ...[[70, 71], [72, 73], [74, 75], [76, 77]].flatMap(([r, a]) => [`func-${r}`, ...[0, 1, 2].map((b) => `func-${a}-bit${b}`)]),
  ];
  return [...front, ...programmer];
}

interface Stop {
  id: string;
  name: string;
  ring: string;
  x: number;
  y: number;
}

/** Presses Tab from the start of the document until focus wraps; returns every stop. */
async function tabStops(page: Page, max: number): Promise<Stop[]> {
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  const stops: Stop[] = [];
  for (let k = 0; k < max; k++) {
    await page.keyboard.press('Tab');
    const s = await page.evaluate(() => {
      const el = document.activeElement as HTMLElement | null;
      if (!el || el === document.body) return null;
      const cs = getComputedStyle(el);
      const r = el.getBoundingClientRect();
      return {
        id: el.dataset.testid ?? `${el.tagName.toLowerCase()}:${el.textContent?.trim()}`,
        name: el.getAttribute('aria-label') ?? '',
        ring: `${cs.outlineWidth} ${cs.outlineStyle} ${cs.outlineColor}`,
        x: r.x,
        y: r.y,
      };
    });
    if (!s || (stops.length > 0 && s.id === stops[0].id)) break;
    stops.push(s);
  }
  return stops;
}

const RING = '2px solid rgb(233, 229, 205)'; // --legend

/**
 * Every Tab stop (enabled, tabindex ≥ 0, rendered). A radio group is one Tab
 * stop in HTML — its checked radio; the arrow keys move within the group.
 */
async function tabbable(page: Page, root = 'body'): Promise<string[]> {
  return page.evaluate((sel) => {
    const els = [...document.querySelector(sel)!.querySelectorAll<HTMLElement>('button, input, select, [tabindex]')];
    return els
      .filter((e) => e.tabIndex >= 0 && !(e as HTMLButtonElement).disabled && e.getClientRects().length > 0)
      .filter((e) => !(e instanceof HTMLInputElement && e.type === 'radio' && e.name && !e.checked))
      .map((e) => e.dataset.testid ?? `${e.tagName.toLowerCase()}:${e.textContent?.trim()}`);
  }, root);
}

const NAME_RULES = new Set([
  'aria-command-name', 'aria-dialog-name', 'aria-input-field-name', 'aria-meter-name', 'aria-progressbar-name',
  'aria-toggle-field-name', 'aria-tooltip-name', 'aria-treeitem-name', 'button-name', 'input-button-name', 'label',
  'link-name', 'select-name', 'svg-img-alt', 'role-img-alt', 'image-alt', 'document-title', 'frame-title',
  'summary-name', 'object-alt', 'area-alt', 'input-image-alt',
]);

async function axeNames(page: Page): Promise<string[]> {
  const res = await new AxeBuilder({ page }).analyze();
  return res.violations
    .filter((v) => NAME_RULES.has(v.id))
    .flatMap((v) => v.nodes.map((n) => `${v.id}: ${n.target.join(' ')}`));
}

test.describe('AC-U5.1 accessibility', () => {
  test('AC-U5.1 Tab reaches every control once, in R-U5 order, with a 2 px --legend ring; axe reports no missing names', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    const all = await tabbable(page);
    const stops = await tabStops(page, all.length + 5);
    // Every interactive control receives focus exactly once …
    expect(stops.map((s) => s.id).sort()).toEqual([...all].sort());
    expect(new Set(stops.map((s) => s.id)).size).toBe(stops.length);
    // … in R-U5 order …
    expect(stops.map((s) => s.id)).toEqual(expectedOrder([1, 2, 3, 4, 5, 6]));
    const firstProgrammer = stops.findIndex((s) => s.id === 'algo-thumb-1');
    for (const s of stops.slice(0, firstProgrammer)) expect(s.y).toBeLessThan(260);
    for (const s of stops.slice(firstProgrammer)) expect(s.y).toBeGreaterThanOrEqual(270);
    // … with a visible ring and an ARIA name.
    expect(stops.filter((s) => s.ring !== RING).map((s) => `${s.id}: ${s.ring}`)).toEqual([]);
    expect(stops.filter((s) => s.id !== 'name-field' && s.name === '').map((s) => s.id)).toEqual([]);
    expect(await axeNames(page)).toEqual([]);
  });

  test('AC-U5.1 with operator order OP6→OP1 the operator panels are tabbed in visual order', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    await page.getByTestId('open-settings').click();
    await page.getByTestId('setting-opOrder-1').check();
    await page.keyboard.press('Escape');
    const stops = await tabStops(page, 400);
    expect(stops.map((s) => s.id)).toEqual(expectedOrder([6, 5, 4, 3, 2, 1]));
  });

  test('AC-U5.1 overlay controls (bank manager, settings) are reachable with Tab, named, ringed; axe reports no missing names', async ({ page }) => {
    test.setTimeout(120_000);
    await launch(page);
    for (const [open, root] of [
      ['cartridge-slot', '[data-testid=bank-manager]'],
      ['open-settings', '[data-testid=settings]'],
    ] as const) {
      await page.getByTestId(open).click();
      await expect(page.locator(root)).toBeVisible();
      const inside = await tabbable(page, root);
      expect(inside.length).toBeGreaterThan(5);
      const seen = new Map<string, string>();
      for (let k = 0; k < 400 && seen.size < inside.length; k++) {
        await page.keyboard.press('Tab');
        const s = await page.evaluate((r) => {
          const el = document.activeElement as HTMLElement;
          if (!el.closest(r)) return null;
          const cs = getComputedStyle(el);
          return { id: el.dataset.testid ?? `${el.tagName.toLowerCase()}:${el.textContent?.trim()}`, ring: `${cs.outlineWidth} ${cs.outlineStyle} ${cs.outlineColor}` };
        }, root);
        if (s) seen.set(s.id, s.ring);
      }
      expect([...seen.keys()].sort()).toEqual([...inside].sort());
      expect([...seen].filter(([, ring]) => ring !== RING)).toEqual([]);
      expect(await axeNames(page)).toEqual([]);
      await page.keyboard.press('Escape');
      await expect(page.locator(root)).toHaveCount(0);
    }
  });
});

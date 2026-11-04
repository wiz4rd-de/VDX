#!/usr/bin/env node
// R-U6 / AC-U6.1: checks the four text/background pairs from theme.css reach
// at least 4.5:1 (WCAG 2 contrast ratio). Usage: node scripts/contrast.mjs [theme.css]
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const MIN = 4.5;
const PAIRS = [
  ['legend', 'programmer', 'programmer text on --programmer'],
  ['legend', 'overlay', 'overlay text on --overlay'],
  ['lcd-ink', 'lcd-bg', 'LCD ink on --lcd-bg'],
  ['body', 'carrier', 'badge text on --carrier'],
  ['body', 'modulator', 'badge text on --modulator'],
];

export function parseTokens(css) {
  const tokens = {};
  for (const m of css.matchAll(/--([a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{3,8})\b/g)) tokens[m[1]] = m[2];
  return tokens;
}

function channel(c) {
  const s = c / 255;
  return s <= 0.03928 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
}

export function luminance(hex) {
  let h = hex.replace('#', '');
  if (h.length === 3) h = [...h].map((x) => x + x).join('');
  const r = parseInt(h.slice(0, 2), 16);
  const g = parseInt(h.slice(2, 4), 16);
  const b = parseInt(h.slice(4, 6), 16);
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

export function contrast(a, b) {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

export function check(css) {
  const t = parseTokens(css);
  return PAIRS.map(([fg, bg, what]) => {
    if (!t[fg] || !t[bg]) return { what, fg, bg, ratio: 0, ok: false, missing: true };
    const ratio = contrast(t[fg], t[bg]);
    return { what, fg, bg, ratio, ok: ratio >= MIN };
  });
}

const here = dirname(fileURLToPath(import.meta.url));
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const file = resolve(process.argv[2] ?? resolve(here, '../src/theme.css'));
  const results = check(readFileSync(file, 'utf8'));
  let failed = 0;
  for (const r of results) {
    const line = r.missing
      ? `MISSING --${r.fg} / --${r.bg}`
      : `${r.ratio.toFixed(2)}:1  --${r.fg} on --${r.bg}  (${r.what})`;
    console.log(`${r.ok ? 'PASS' : 'FAIL'}  ${line}`);
    if (!r.ok) failed++;
  }
  if (failed) {
    console.error(`contrast: ${failed} pair(s) below ${MIN}:1`);
    process.exit(1);
  }
  console.log(`contrast: all ${results.length} checks >= ${MIN}:1`);
}

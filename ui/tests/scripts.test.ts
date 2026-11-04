import { describe, it, expect } from 'vitest';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, mkdirSync, readFileSync, rmSync, readdirSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { build } from 'vite';

const UI = resolve(__dirname, '..');
const node = process.execPath;
const run = (script: string, ...args: string[]) =>
  spawnSync(node, [join(UI, 'scripts', script), ...args], { cwd: UI, encoding: 'utf8' });

describe('AC-U6.1 contrast', () => {
  it('AC-U6.1 every R-U6 pair in theme.css reaches 4.5:1 (npm run contrast passes)', () => {
    const r = run('contrast.mjs');
    expect(r.status, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain('--legend on --programmer');
    expect(r.stdout).toContain('--legend on --overlay');
    expect(r.stdout).toContain('--lcd-ink on --lcd-bg');
    expect(r.stdout).toContain('--body on --carrier');
    expect(r.stdout).toContain('--body on --modulator');
    expect(r.stdout).not.toContain('FAIL');
  });

  it('AC-U6.1 the contrast script fails when a pair is below 4.5:1', () => {
    const dir = mkdtempSync(join(tmpdir(), 'vdx-contrast-'));
    const css = readFileSync(join(UI, 'src/theme.css'), 'utf8').replace('#1C2216', '#7A836C');
    writeFileSync(join(dir, 'theme.css'), css);
    const r = run('contrast.mjs', join(dir, 'theme.css'));
    rmSync(dir, { recursive: true, force: true });
    expect(r.status).toBe(1);
    expect(r.stdout).toContain('FAIL');
  });

  it('AC-U6.1 theme.css holds the §11 tokens exactly', () => {
    const css = readFileSync(join(UI, 'src/theme.css'), 'utf8');
    const want: Record<string, string> = {
      body: '#2B211B', panel: '#4A392C', programmer: '#3A2D24', overlay: '#322720',
      'dx-green': '#63B39B', 'dx-green-lit': '#9BE3C9', violet: '#9A86C4', 'brown-legend': '#C3926A',
      salmon: '#EE8A76', legend: '#E9E5CD', 'lcd-bg': '#9BA48B', 'lcd-ink': '#1C2216', 'lcd-off': '#8E977F',
      'led-on': '#FF3B2F', 'led-off': '#3A1512', carrier: '#E9A15B', modulator: '#6FA8DC',
    };
    for (const [k, v] of Object.entries(want)) expect(css).toMatch(new RegExp(`--${k}:\\s*${v};`));
  });
});

describe('AC-SC3 no raster assets', () => {
  it('AC-SC3 a fresh build of ui/dist contains no raster files or data:image raster URIs', async () => {
    const out = mkdtempSync(join(tmpdir(), 'vdx-dist-'));
    try {
      await build({ root: UI, logLevel: 'silent', build: { outDir: out, emptyOutDir: true } });
      const r = run('raster-check.mjs', out);
      expect(r.status, r.stdout + r.stderr).toBe(0);
      const index = readFileSync(join(out, 'index.html'), 'utf8');
      expect(index).toContain('<div id="app">');
    } finally {
      rmSync(out, { recursive: true, force: true });
    }
  });

  it('AC-SC3 the raster check fails on png/jpg/gif/webp/bmp files and raster data URIs', () => {
    for (const bad of ['a.png', 'b.jpg', 'c.gif', 'd.webp', 'e.bmp']) {
      const dir = mkdtempSync(join(tmpdir(), 'vdx-raster-'));
      writeFileSync(join(dir, bad), 'x');
      expect(run('raster-check.mjs', dir).status).toBe(1);
      rmSync(dir, { recursive: true, force: true });
    }
    const dir = mkdtempSync(join(tmpdir(), 'vdx-raster-'));
    mkdirSync(join(dir, 'assets'));
    writeFileSync(join(dir, 'assets/x.css'), 'a{background:url(data:image/png;base64,AAAA)}');
    expect(run('raster-check.mjs', dir).status).toBe(1);
    writeFileSync(join(dir, 'assets/x.css'), 'a{background:url("data:image/svg+xml,<svg/>")}');
    expect(run('raster-check.mjs', dir).status).toBe(0);
    rmSync(dir, { recursive: true, force: true });
  });
});

describe('R-L5 branding (ui/src)', () => {
  it('R-L5 ui/src never contains YAMAHA or DX7', () => {
    const hits: string[] = [];
    const walk = (dir: string): void => {
      for (const name of readdirSync(dir)) {
        const p = join(dir, name);
        if (statSync(p).isDirectory()) walk(p);
        else if (!name.endsWith('.woff2')) {
          const text = readFileSync(p, 'latin1');
          if (/yamaha|dx7/i.test(text)) hits.push(p);
        }
      }
    };
    walk(join(UI, 'src'));
    expect(hits).toEqual([]);
  });
});

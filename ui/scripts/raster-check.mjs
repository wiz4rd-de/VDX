#!/usr/bin/env node
// AC-SC3: fails if the UI build contains raster images (png/jpg/gif/webp/bmp
// and other raster formats) or `data:image/` raster URIs. SVG is vector and
// allowed. Usage: node scripts/raster-check.mjs [dir]   (default: ui/dist)
import { readdirSync, readFileSync, statSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve, join, extname, relative } from 'node:path';

const RASTER_EXT = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.ico', '.avif', '.tif', '.tiff', '.apng']);
const TEXT_EXT = new Set(['.html', '.htm', '.js', '.mjs', '.css', '.svg', '.json', '.map', '.txt', '.webmanifest']);
const RASTER_URI = /data:image\/(?!svg\+xml)[a-z0-9.+-]+/gi;

function walk(dir, out = []) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else out.push(p);
  }
  return out;
}

export function scan(dir) {
  const problems = [];
  for (const file of walk(dir)) {
    const ext = extname(file).toLowerCase();
    const rel = relative(dir, file);
    if (RASTER_EXT.has(ext)) problems.push(`raster file: ${rel}`);
    else if (TEXT_EXT.has(ext)) {
      const text = readFileSync(file, 'utf8');
      for (const m of text.matchAll(RASTER_URI)) problems.push(`raster URI in ${rel}: ${m[0]}`);
    }
  }
  return problems;
}

const here = dirname(fileURLToPath(import.meta.url));
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const dir = resolve(process.argv[2] ?? resolve(here, '../dist'));
  if (!existsSync(dir)) {
    console.error(`raster-check: ${dir} does not exist (run npm run build first)`);
    process.exit(1);
  }
  const problems = scan(dir);
  for (const p of problems) console.error(`FAIL  ${p}`);
  if (problems.length) {
    console.error(`raster-check: ${problems.length} problem(s) in ${dir}`);
    process.exit(1);
  }
  console.log(`raster-check: no raster images in ${dir}`);
}

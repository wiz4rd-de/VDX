// VDX UI build (SPEC §9.4: Svelte 5 + TypeScript, one bundle for every build).
// Dev and preview both serve on http://localhost:5173 with cross-origin
// isolation headers (SPEC §15.2) so the WASM core can use shared memory.
import { defineConfig } from 'vitest/config';
import type { Plugin } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

// The SIL OFL requires the licence to travel with the bundled fonts.
function fontLicences(): Plugin {
  const dir = fileURLToPath(new URL('./src/fonts/', import.meta.url));
  return {
    name: 'vdx-font-licences',
    apply: 'build',
    generateBundle() {
      for (const f of ['OFL-BarlowCondensed.txt', 'OFL-Oswald.txt'])
        this.emitFile({ type: 'asset', fileName: `fonts/${f}`, source: readFileSync(dir + f, 'utf8') });
    },
  };
}

export default defineConfig({
  plugins: [svelte(), fontLicences()],
  // Relative asset URLs: the desktop shell serves ui/dist from a resource provider.
  base: './',
  server: { port: 5173, strictPort: true, headers: isolationHeaders },
  preview: { port: 5173, strictPort: true, headers: isolationHeaders },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    // Never inline assets as data: URIs (fonts stay files; AC-SC3 forbids raster URIs).
    assetsInlineLimit: 0,
    target: 'es2022',
  },
  resolve: process.env.VITEST ? { conditions: ['browser'] } : undefined,
  test: {
    include: ['tests/**/*.test.ts'],
    environment: 'node',
    testTimeout: 60000,
  },
});

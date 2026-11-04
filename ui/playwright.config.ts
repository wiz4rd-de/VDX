// End-to-end suites (SPEC §13: UI criteria run with Playwright against the
// web build in Chromium and WebKit). They drive the real UI and the real WASM
// core served by `npm run web:e2e` (test build with the analyser tap, §10).
// `npm run web:noiso` serves the same page without COOP/COEP for AC-SC2.
import { defineConfig, devices } from '@playwright/test';

const CI = !!process.env.CI;
const viewport = { width: 1280, height: 1000 };

export default defineConfig({
  testDir: './e2e',
  testMatch: '**/*.spec.ts',
  timeout: 60_000,
  expect: {
    timeout: 5_000,
    // AC-SC1: differences from the committed baselines ≤ 0.1 % of pixels.
    toHaveScreenshot: { maxDiffPixelRatio: 0.001, animations: 'disabled', caret: 'hide' },
  },
  fullyParallel: true,
  // Audio worklets and the 10 s dump timeout are timing-sensitive: keep the
  // machine unloaded.
  workers: CI ? 2 : 3,
  retries: CI ? 1 : 0,
  reporter: CI ? [['list'], ['html', { open: 'never' }]] : [['list']],
  use: {
    baseURL: 'http://localhost:5173',
    viewport,
    deviceScaleFactor: 1,
    trace: 'retain-on-failure',
    acceptDownloads: true,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        viewport,
        deviceScaleFactor: 1,
        launchOptions: { args: ['--autoplay-policy=no-user-gesture-required'] },
      },
    },
    {
      name: 'webkit',
      use: { ...devices['Desktop Safari'], viewport, deviceScaleFactor: 1 },
    },
  ],
  webServer: [
    {
      command: 'npm run web:e2e',
      url: 'http://localhost:5173',
      reuseExistingServer: !CI,
      timeout: 120_000,
    },
    {
      command: 'npm run web:noiso',
      url: 'http://localhost:5174',
      reuseExistingServer: !CI,
      timeout: 120_000,
    },
  ],
});

import { defineConfig, devices } from '@playwright/test';

/**
 * End-to-end tests run against a REAL stack (no mocks): start it with
 *   ./scripts/start-demo.sh
 * and point STUDIO_URL elsewhere if it is not on the default port. Set
 * PLAYWRIGHT_CHROMIUM to an existing Chromium binary to skip `npx playwright install`.
 */
export default defineConfig({
  testDir: './e2e',
  timeout: 90_000,
  expect: { timeout: 20_000 },
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: [['list']],
  use: {
    baseURL: process.env.STUDIO_URL ?? 'http://127.0.0.1:8080',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        ...(process.env.PLAYWRIGHT_CHROMIUM ? { launchOptions: { executablePath: process.env.PLAYWRIGHT_CHROMIUM } } : {}),
      },
    },
  ],
});

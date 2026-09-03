// @ts-check
const { defineConfig } = require('@playwright/test');

const baseURL = process.env.AETHER_BPP_BASE_URL || 'http://127.0.0.1:4173';
const skipTls = process.env.AETHER_BPP_SKIP_TLS === '1' ||
  process.env.AETHER_BPP_SKIP_TLS === 'true';

const live =
  process.env.AETHER_BPP_LIVE === '1' || process.env.AETHER_BPP_LIVE === 'true';

module.exports = defineConfig({
  testDir: './',
  timeout: live ? 600_000 : 120_000,
  expect: { timeout: live ? 120_000 : 30_000 },
  fullyParallel: false,
  retries: 0,
  use: {
    baseURL,
    headless: true,
    ignoreHTTPSErrors: !!process.env.AETHER_BPP_TRUST_LOCAL_CA,
  },
  metadata: {
    skipTls,
  },
});

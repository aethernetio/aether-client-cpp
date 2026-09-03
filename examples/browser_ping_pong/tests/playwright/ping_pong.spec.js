// @ts-check
const { test, expect } = require('@playwright/test');

/**
 * Dual-context profile A/B ping-pong smoke.
 * Requires a static host of the CMake `web/` output and a running gateway.
 * Set AETHER_BPP_BASE_URL (default http://127.0.0.1:4173).
 * Set AETHER_BPP_GATEWAY for the browser gateway URL.
 * TLS transports skip when AETHER_BPP_SKIP_TLS=1 or certs are missing.
 */

const gatewayDefault = process.env.AETHER_BPP_GATEWAY || 'ws://127.0.0.1:8080';

async function openProfile(browser, profile, transport, gateway) {
  const context = await browser.newContext();
  const page = await context.newPage();
  const params = new URLSearchParams({
    profile,
    transport,
    gateway,
  });
  await page.goto(`/?${params.toString()}`);
  await page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 60_000,
  });
  return { context, page };
}

async function waitReady(page) {
  await page.waitForFunction(() => {
    const s = window.__AETHER_TEST__.getState();
    return s.startsWith('Ready') || s.startsWith('Connected');
  }, null, { timeout: 90_000 });
}

async function exchangeOnce(pageA, pageB) {
  const uidA = await pageA.evaluate(() => window.__AETHER_TEST__.getUid());
  const uidB = await pageB.evaluate(() => window.__AETHER_TEST__.getUid());
  expect(uidA).toMatch(/^[0-9a-f-]{36}$/i);
  expect(uidB).toMatch(/^[0-9a-f-]{36}$/i);

  await pageB.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, uidA);

  await pageA.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, uidB);

  await pageA.waitForFunction(() =>
    window.__AETHER_TEST__.getState().startsWith('Connected'), null, {
    timeout: 60_000,
  });

  const before = await pageA.evaluate(() => window.__AETHER_TEST__.getStats());
  await pageA.evaluate(() => window.__AETHER_TEST__.sendPing());
  await pageA.waitForFunction((prev) => {
    const s = window.__AETHER_TEST__.getStats();
    return s.pong_received > (prev.pong_received || 0);
  }, before, { timeout: 30_000 });

  const after = await pageA.evaluate(() => window.__AETHER_TEST__.getStats());
  expect(after.sent).toBeGreaterThan(before.sent || 0);
  expect(after.pong_received).toBeGreaterThan(before.pong_received || 0);
}

function transportTest(tag, transport, gateway, { requiresTls = false } = {}) {
  test(`profiles A/B ping-pong (${transport}) ${tag}`, async ({ browser }) => {
    test.skip(
      requiresTls &&
        (process.env.AETHER_BPP_SKIP_TLS === '1' ||
          process.env.AETHER_BPP_SKIP_TLS === 'true' ||
          !process.env.AETHER_BPP_TLS_READY),
      'TLS certs not configured (set AETHER_BPP_TLS_READY=1 and unset AETHER_BPP_SKIP_TLS)',
    );

    const a = await openProfile(browser, 'A', transport, gateway);
    const b = await openProfile(browser, 'B', transport, gateway);
    try {
      await waitReady(a.page);
      await waitReady(b.page);
      await exchangeOnce(a.page, b.page);

      const stateA = await a.page.locator('[data-testid="state"]').getAttribute('data-state');
      expect(stateA).toBeTruthy();
    } finally {
      await a.context.close();
      await b.context.close();
    }
  });
}

test.describe('browser_ping_pong transports', () => {
  transportTest('@ws', 'WS', process.env.AETHER_BPP_GATEWAY_WS || gatewayDefault);
  transportTest(
    '@http',
    'HTTP',
    process.env.AETHER_BPP_GATEWAY_HTTP || 'http://127.0.0.1:8080',
  );
  transportTest(
    '@wss',
    'WSS',
    process.env.AETHER_BPP_GATEWAY_WSS || 'wss://127.0.0.1:8443',
    { requiresTls: true },
  );
  transportTest(
    '@https',
    'HTTPS',
    process.env.AETHER_BPP_GATEWAY_HTTPS || 'https://127.0.0.1:8443',
    { requiresTls: true },
  );
});

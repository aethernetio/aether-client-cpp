// @ts-check
const { test, expect } = require('@playwright/test');

/**
 * Live Æther cloud e2e (NOT the test-only browser_transport_gateway).
 *
 * Required:
 *   AETHER_BPP_LIVE=1
 *   AETHER_BPP_GATEWAY=wss://dbservice.aethernet.io:9013/
 *   AETHER_BPP_BASE_URL pointing at the built static web/ output
 *
 * Optional:
 *   AETHER_BPP_PING_COUNT (default 100)
 *   AETHER_BPP_TRANSPORT (default WSS)
 */

function requireLiveGateway() {
  if (process.env.AETHER_BPP_LIVE !== '1' && process.env.AETHER_BPP_LIVE !== 'true') {
    test.skip(true, 'Set AETHER_BPP_LIVE=1 to run live cloud tests');
  }
  const gateway = process.env.AETHER_BPP_GATEWAY || '';
  if (!gateway) {
    throw new Error('AETHER_BPP_GATEWAY is required for live tests');
  }
  const lower = gateway.toLowerCase();
  if (
    lower.includes('127.0.0.1') ||
    lower.includes('localhost') ||
    lower.includes('/aether/v1')
  ) {
    throw new Error(
      `Refusing test-only gateway for live run: ${gateway}. ` +
        'Use a production FastMeta WSS/HTTP endpoint (path "/").',
    );
  }
  return gateway;
}

async function openProfile(browser, profile, transport, gateway) {
  const context = await browser.newContext();
  const page = await context.newPage();
  const params = new URLSearchParams({
    profile,
    transport,
    gateway,
    autostart: '0',
  });
  await page.goto(`/?${params.toString()}`);
  await page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  return { context, page };
}

async function clearAndStart(page) {
  await page.evaluate(async () => {
    const api = window.__AETHER_TEST__;
    if (api.clearProfile) {
      api.clearProfile();
    }
    await new Promise((r) => setTimeout(r, 300));
    api.start();
  });
  await page.waitForFunction(() => {
    const s = window.__AETHER_TEST__.getState();
    const persisted =
      !window.__AETHER_TEST__.storagePersisted ||
      window.__AETHER_TEST__.storagePersisted();
    return (
      (s.startsWith('Ready') && persisted) ||
      s.startsWith('Connected') ||
      s.startsWith('Error')
    );
  }, null, { timeout: 180_000 });
  const state = await page.evaluate(() => window.__AETHER_TEST__.getState());
  if (String(state).startsWith('Error')) {
    throw new Error(`clearAndStart failed: ${state}`);
  }
}

test.describe('live Æther cloud', () => {
  test('register, reload UID, P2P, 100 ping/pong @live', async ({ browser }) => {
    const gateway = requireLiveGateway();
    const transport = (process.env.AETHER_BPP_TRANSPORT || 'WSS').toUpperCase();
    const pingCount = Number(process.env.AETHER_BPP_PING_COUNT || 100);

    const a = await openProfile(browser, 'A', transport, gateway);
    const b = await openProfile(browser, 'B', transport, gateway);
    try {
      await clearAndStart(a.page);
      await clearAndStart(b.page);

      const uidA1 = await a.page.evaluate(() => window.__AETHER_TEST__.getUid());
      const uidB1 = await b.page.evaluate(() => window.__AETHER_TEST__.getUid());
      expect(uidA1).toMatch(/^[0-9a-f-]{36}$/i);
      expect(uidB1).toMatch(/^[0-9a-f-]{36}$/i);
      expect(uidA1).not.toEqual(uidB1);

      await a.page.reload();
      await b.page.reload();
      await a.page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
        timeout: 120_000,
      });
      await b.page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
        timeout: 120_000,
      });
      // Reload keeps autostart=0 from the query string — start manually.
      await a.page.evaluate(() => window.__AETHER_TEST__.start());
      await b.page.evaluate(() => window.__AETHER_TEST__.start());
      await a.page.waitForFunction(() => {
        const s = window.__AETHER_TEST__.getState();
        const persisted =
          !window.__AETHER_TEST__.storagePersisted ||
          window.__AETHER_TEST__.storagePersisted();
        return (s.startsWith('Ready') && persisted) || s.startsWith('Connected');
      }, null, { timeout: 180_000 });
      await b.page.waitForFunction(() => {
        const s = window.__AETHER_TEST__.getState();
        const persisted =
          !window.__AETHER_TEST__.storagePersisted ||
          window.__AETHER_TEST__.storagePersisted();
        return (s.startsWith('Ready') && persisted) || s.startsWith('Connected');
      }, null, { timeout: 180_000 });

      const uidA2 = await a.page.evaluate(() => window.__AETHER_TEST__.getUid());
      const uidB2 = await b.page.evaluate(() => window.__AETHER_TEST__.getUid());
      expect(uidA2).toBe(uidA1);
      expect(uidB2).toBe(uidB1);

      // One-sided initiate: A opens to B; B accepts inbound P2P port.
      await a.page.evaluate((uid) => {
        window.__AETHER_TEST__.setRemoteUid(uid);
        window.__AETHER_TEST__.connect();
      }, uidB2);
      await b.page.evaluate((uid) => {
        window.__AETHER_TEST__.setRemoteUid(uid);
      }, uidA2);

      await a.page.waitForFunction(
        () => window.__AETHER_TEST__.getState().startsWith('Connected'),
        null,
        { timeout: 120_000 },
      );

      await a.page.evaluate((n) => {
        // Cap concurrency via kMaxInFlight=16; keep interval above typical RTT.
        window.__AETHER_TEST__.startPeriodic(80);
        const started = Date.now();
        const watch = setInterval(() => {
          const stats = window.__AETHER_TEST__.getStats();
          if ((stats.pong_received || 0) >= n || Date.now() - started > 280_000) {
            clearInterval(watch);
            window.__AETHER_TEST__.stop();
          }
        }, 200);
      }, pingCount);

      await a.page.waitForFunction(
        (n) => (window.__AETHER_TEST__.getStats().pong_received || 0) >= n,
        pingCount,
        { timeout: 300_000 },
      );

      const stats = await a.page.evaluate(() => window.__AETHER_TEST__.getStats());
      expect(stats.pong_received).toBeGreaterThanOrEqual(pingCount);
      expect(stats.duplicate || 0).toBe(0);
      expect(stats.out_of_order || 0).toBe(0);
      const lost = Math.max(0, (stats.sent || 0) - (stats.pong_received || 0));
      expect(lost).toBe(0);

      console.log(
        JSON.stringify(
          {
            transport,
            gateway,
            uidA: uidA2,
            uidB: uidB2,
            sent: stats.sent,
            pong_received: stats.pong_received,
            lost,
            duplicate: stats.duplicate || 0,
            out_of_order: stats.out_of_order || 0,
            rtt_min_ms: stats.rtt_min_ms,
            rtt_avg_ms: stats.rtt_avg_ms,
            rtt_p50_ms: stats.rtt_p50_ms,
            rtt_p90_ms: stats.rtt_p90_ms,
            rtt_p95_ms: stats.rtt_p95_ms,
            rtt_p99_ms: stats.rtt_p99_ms,
            rtt_max_ms: stats.rtt_max_ms,
          },
          null,
          2,
        ),
      );
    } finally {
      await a.context.close();
      await b.context.close();
    }
  });
});

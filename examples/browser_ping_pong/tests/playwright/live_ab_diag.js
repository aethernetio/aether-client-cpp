const { chromium } = require('@playwright/test');

const gateway =
  process.env.AETHER_BPP_GATEWAY || 'wss://dbservice.aethernet.io:9013/';
const base = process.env.AETHER_BPP_BASE_URL || 'http://127.0.0.1:4173';
const pingCount = Number(process.env.AETHER_BPP_PING_COUNT || 100);
const transport = (process.env.AETHER_BPP_TRANSPORT || 'WSS').toUpperCase();

async function open(browser, profile) {
  const context = await browser.newContext();
  const page = await context.newPage();
  page.on('console', (msg) => {
    const t = msg.text();
    if (
      /RegisterStarted|Client registered|Ready:|WS linked|Error|P2P|Accepted inbound|PAGEERROR|memory access/i.test(
        t,
      )
    ) {
      console.log(`[${profile}]`, t.slice(0, 200));
    }
  });
  page.on('pageerror', (err) => console.log(`[${profile}] PAGEERROR`, err.message));
  const params = new URLSearchParams({
    profile,
    transport,
    gateway,
    autostart: '0',
  });
  await page.goto(`${base}/?${params.toString()}`);
  await page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  return { context, page, profile };
}

async function waitReadyPersisted(page, label) {
  await page.waitForFunction(
    () => {
      const s = window.__AETHER_TEST__.getState();
      const persisted =
        !window.__AETHER_TEST__.storagePersisted ||
        window.__AETHER_TEST__.storagePersisted();
      return (
        (s.startsWith('Ready') && persisted) ||
        s.startsWith('Connected') ||
        s.startsWith('Error')
      );
    },
    null,
    { timeout: 180_000 },
  );
  const snap = await page.evaluate(() => ({
    state: window.__AETHER_TEST__.getState(),
    uid: window.__AETHER_TEST__.getUid(),
    persisted: window.__AETHER_TEST__.storagePersisted
      ? window.__AETHER_TEST__.storagePersisted()
      : null,
  }));
  console.log(label, JSON.stringify(snap));
  if (String(snap.state).startsWith('Error')) {
    throw new Error(`${label} error: ${snap.state}`);
  }
  return snap;
}

(async () => {
  const browser = await chromium.launch();
  const a = await open(browser, 'A');
  const b = await open(browser, 'B');

  console.log('=== clear + register ===');
  for (const p of [a, b]) {
    await p.page.evaluate(async () => {
      window.__AETHER_TEST__.clearProfile();
      await new Promise((r) => setTimeout(r, 300));
      window.__AETHER_TEST__.start();
    });
  }
  const regA = await waitReadyPersisted(a.page, 'A registered');
  const regB = await waitReadyPersisted(b.page, 'B registered');

  console.log('=== reload persistence ===');
  await a.page.reload();
  await b.page.reload();
  await a.page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  await b.page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  await a.page.evaluate(() => window.__AETHER_TEST__.start());
  await b.page.evaluate(() => window.__AETHER_TEST__.start());
  const reloadA = await waitReadyPersisted(a.page, 'A after reload');
  const reloadB = await waitReadyPersisted(b.page, 'B after reload');
  if (reloadA.uid !== regA.uid || reloadB.uid !== regB.uid) {
    throw new Error(
      `UID mismatch after reload A:${regA.uid}->${reloadA.uid} B:${regB.uid}->${reloadB.uid}`,
    );
  }
  console.log('UID persistence PASS');

  console.log('=== P2P connect (both sides CreatePort for receive) ===');
  await a.page.waitForTimeout(5000);
  await b.page.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, reloadA.uid);
  await a.page.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, reloadB.uid);

  for (let i = 0; i < 90; i++) {
    await a.page.waitForTimeout(2000);
    const sa = await a.page.evaluate(() => window.__AETHER_TEST__.getState());
    const sb = await b.page.evaluate(() => window.__AETHER_TEST__.getState());
    console.log(`T+${i * 2}s A=${sa} | B=${sb}`);
    if (sa.startsWith('Connected') && sb.startsWith('Connected')) {
      break;
    }
    if (sa.startsWith('Error') || sb.startsWith('Error')) {
      throw new Error(`P2P error A=${sa} B=${sb}`);
    }
  }

  await a.page.waitForFunction(
    () => window.__AETHER_TEST__.getState().startsWith('Connected'),
    null,
    { timeout: 180_000 },
  );
  await b.page.waitForFunction(
    () => window.__AETHER_TEST__.getState().startsWith('Connected'),
    null,
    { timeout: 180_000 },
  );
  console.log('P2P Connected (A+B)');

  console.log(`=== ${pingCount} ping/pong ===`);
  await a.page.evaluate((n) => {
    window.__AETHER_TEST__.startPeriodic(100);
    const started = Date.now();
    const watch = setInterval(() => {
      const stats = window.__AETHER_TEST__.getStats();
      if ((stats.pong_received || 0) >= n || Date.now() - started > 280_000) {
        clearInterval(watch);
        window.__AETHER_TEST__.stop();
      }
    }, 500);
  }, pingCount);

  for (let i = 0; i < 60; i++) {
    await a.page.waitForTimeout(5000);
    const stats = await a.page.evaluate(() => window.__AETHER_TEST__.getStats());
    const sa = await a.page.evaluate(() => window.__AETHER_TEST__.getState());
    const sb = await b.page.evaluate(() => window.__AETHER_TEST__.getState());
    console.log(
      `ping T+${(i + 1) * 5}s sent=${stats.sent} pong=${stats.pong_received} ` +
        `to=${stats.timed_out} mal=${stats.malformed} A=${sa} B=${sb}`,
    );
    if ((stats.pong_received || 0) >= pingCount) {
      break;
    }
  }

  await a.page.waitForFunction(
    (n) => (window.__AETHER_TEST__.getStats().pong_received || 0) >= n,
    pingCount,
    { timeout: 60_000 },
  );

  const stats = await a.page.evaluate(() => window.__AETHER_TEST__.getStats());
  const lost = Math.max(0, (stats.sent || 0) - (stats.pong_received || 0));
  const result = {
    transport,
    gateway,
    uidA: reloadA.uid,
    uidB: reloadB.uid,
    sent: stats.sent,
    pong_received: stats.pong_received,
    lost,
    duplicate: stats.duplicate || 0,
    out_of_order: stats.out_of_order || 0,
    timed_out: stats.timed_out || 0,
    rtt_min_ms: stats.rtt_min_ms,
    rtt_avg_ms: stats.rtt_avg_ms,
    rtt_p50_ms: stats.rtt_p50_ms,
    rtt_p90_ms: stats.rtt_p90_ms,
    rtt_p95_ms: stats.rtt_p95_ms,
    rtt_p99_ms: stats.rtt_p99_ms,
    rtt_max_ms: stats.rtt_max_ms,
  };
  console.log('RESULT', JSON.stringify(result, null, 2));
  if (lost > 0 || (stats.duplicate || 0) > 0) {
    process.exitCode = 2;
  }
  await a.context.close();
  await b.context.close();
  await browser.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});

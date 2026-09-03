'use strict';

/**
 * Live A/B P2P 100 ping/pong over production WSS only.
 * Refuses localhost/test-gateway as live proof.
 */

const { chromium } = require('@playwright/test');

const gateway =
  process.env.AETHER_BPP_GATEWAY || 'wss://dbservice.aethernet.io:9013/';
const base = process.env.AETHER_BPP_BASE_URL || 'http://127.0.0.1:4173';
const transport = (process.env.AETHER_BPP_TRANSPORT || 'WSS').toUpperCase();
const targetPongs = Number(process.env.AETHER_BPP_TARGET_PONGS || '100');

function assertLiveGateway(url) {
  const u = String(url || '');
  if (!/^wss?:\/\//i.test(u)) {
    throw new Error(`Live proof requires WS/WSS gateway, got: ${u}`);
  }
  if (/localhost|127\.0\.0\.1|0\.0\.0\.0|\[::1\]/i.test(u)) {
    throw new Error(`Refusing localhost gateway as live proof: ${u}`);
  }
  if (/gateway|4174|9019|test/i.test(u) && !/aethernet\.io/i.test(u)) {
    throw new Error(`Refusing test gateway as live proof: ${u}`);
  }
}

async function open(browser, profile) {
  const context = await browser.newContext();
  const page = await context.newPage();
  const logs = [];
  page.on('console', (msg) => {
    const t = msg.text();
    logs.push(t);
    if (
      /Ping received|Ping timeout|Ready:|Connected|Error|NewMessage|Write |P2P|send_message|Accepted|PAGEERROR|pong|frame/i.test(
        t,
      )
    ) {
      console.log(`[${profile}]`, t.slice(0, 240));
    }
  });
  page.on('pageerror', (err) => {
    console.log(`[${profile}] PAGEERROR`, err.message);
    logs.push('PAGEERROR ' + err.message);
  });
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
  return { context, page, profile, logs };
}

async function waitReady(page, label) {
  await page.waitForFunction(
    () => {
      const s = window.__AETHER_TEST__.getState();
      return (
        s.startsWith('Ready') ||
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
  }));
  console.log(label, JSON.stringify(snap));
  if (String(snap.state).startsWith('Error')) {
    throw new Error(`${label}: ${snap.state}`);
  }
  if (!snap.uid) {
    throw new Error(`${label}: empty uid`);
  }
  return snap;
}

(async () => {
  assertLiveGateway(gateway);
  if (transport !== 'WSS' && transport !== 'WS') {
    throw new Error(`Live A/B requires WS/WSS transport, got ${transport}`);
  }

  console.log(
    JSON.stringify({ gateway, transport, base, targetPongs }, null, 0),
  );

  const browser = await chromium.launch();
  const a = await open(browser, 'LiveP2P_A');
  const b = await open(browser, 'LiveP2P_B');

  for (const p of [a, b]) {
    await p.page.evaluate(async () => {
      window.__AETHER_TEST__.clearProfile();
      await new Promise((r) => setTimeout(r, 250));
      window.__AETHER_TEST__.start();
    });
  }

  const regA = await waitReady(a.page, 'A ready');
  const regB = await waitReady(b.page, 'B ready');

  // Brief settle so work-cloud WSS login+ping completes on both.
  await a.page.waitForTimeout(8000);

  await b.page.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, regA.uid);
  await a.page.evaluate((uid) => {
    window.__AETHER_TEST__.setRemoteUid(uid);
    window.__AETHER_TEST__.connect();
  }, regB.uid);

  for (let i = 0; i < 45; i++) {
    await a.page.waitForTimeout(2000);
    const sa = await a.page.evaluate(() => window.__AETHER_TEST__.getState());
    const sb = await b.page.evaluate(() => window.__AETHER_TEST__.getState());
    console.log(`connect T+${i * 2}s A=${sa} | B=${sb}`);
    if (sa.startsWith('Connected') && sb.startsWith('Connected')) {
      break;
    }
    if (sa.startsWith('Error') || sb.startsWith('Error')) {
      throw new Error(`connect failed A=${sa} B=${sb}`);
    }
  }

  // ~100 pings at 200ms => ~20s; allow generous drain window.
  await a.page.evaluate(() => {
    window.__AETHER_TEST__.startPeriodic(200);
  });

  let stats = { pong_received: 0, sent: 0 };
  for (let i = 0; i < 90; i++) {
    await a.page.waitForTimeout(2000);
    stats = await a.page.evaluate(() => window.__AETHER_TEST__.getStats());
    const sb = await b.page.evaluate(() => window.__AETHER_TEST__.getStats());
    console.log(
      `p2p T+${i * 2}s A sent=${stats.sent} pong=${stats.pong_received} timed_out=${stats.timed_out} | B pong=${sb.pong_received}`,
    );
    if (Number(stats.pong_received) >= targetPongs) {
      break;
    }
  }

  await a.page.evaluate(() => window.__AETHER_TEST__.stop());
  stats = await a.page.evaluate(() => window.__AETHER_TEST__.getStats());
  console.log('FINAL_STATS', JSON.stringify(stats));

  // Reload A and confirm same UID (IndexedDB persistence).
  await a.page.reload();
  await a.page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  await a.page.evaluate(async () => {
    // Do not clear — reload with preserved profile.
    window.__AETHER_TEST__.start();
  });
  const reloaded = await waitReady(a.page, 'A reload');
  const uidMatch = reloaded.uid === regA.uid;
  console.log(
    'RELOAD_UID',
    JSON.stringify({ before: regA.uid, after: reloaded.uid, match: uidMatch }),
  );

  await a.context.close();
  await b.context.close();
  await browser.close();

  const pongs = Number(stats.pong_received) || 0;
  if (pongs < targetPongs) {
    console.error(`FAIL: pong_received=${pongs} < ${targetPongs}`);
    process.exit(2);
  }
  if (!uidMatch) {
    console.error('FAIL: UID changed after reload');
    process.exit(3);
  }
  console.log(`PASS: ${pongs} P2P pongs over ${transport}, UID persisted`);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});

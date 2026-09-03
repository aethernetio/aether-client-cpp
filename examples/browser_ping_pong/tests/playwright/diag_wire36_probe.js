'use strict';

/**
 * Minimal live WSS probe: login + encrypted AuthorizedApi.ping only.
 * Expects protocol method-36 fix (no void pull_messages before ping).
 */

const { chromium } = require('@playwright/test');

const gateway =
  process.env.AETHER_BPP_GATEWAY || 'wss://dbservice.aethernet.io:9013/';
const base = process.env.AETHER_BPP_BASE_URL || 'http://127.0.0.1:4173';
const workServerId = Number(process.env.AETHER_BPP_WORK_SERVER_ID || '20');

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const logs = [];
  let pingReceived = false;
  let pingTimeout = false;
  let wsRecvAfterLogin = 0;
  let workOpen = false;

  page.on('console', (msg) => {
    const t = msg.text();
    logs.push(t);
    if (/Ping received|Ping timeout|Ping server|Login api|ws open|ws recv|ws send|Work cloud filtered|Ready|Error|encrypted size/i.test(t)) {
      console.log(t.slice(0, 280));
    }
    if (/Ping received/i.test(t)) {
      pingReceived = true;
    }
    if (/Ping timeout/i.test(t)) {
      pingTimeout = true;
    }
    if (new RegExp(`ws open .*9023`).test(t) || /ws open \d+ wss:\/\/dbservice\.aethernet\.io:9023/.test(t)) {
      workOpen = true;
    }
    if (/ws recv \d+ wss:\/\/.*:9023\//.test(t)) {
      wsRecvAfterLogin += 1;
    }
  });

  const params = new URLSearchParams({
    profile: 'Wire36Probe',
    transport: 'WSS',
    gateway,
    autostart: '0',
  });
  await page.goto(`${base}/?${params.toString()}`);
  await page.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120000,
  });

  await page.evaluate(async (sid) => {
    if (typeof Module !== 'undefined' && Module.cwrap) {
      try {
        const setFilter = Module.cwrap(
          'aether_bpp_set_work_server_filter',
          null,
          ['number'],
        );
        setFilter(sid);
      } catch (e) {
        console.log('work_server_filter cwrap missing', String(e));
      }
    }
    window.__AETHER_TEST__.clearProfile();
    await new Promise((r) => setTimeout(r, 300));
    window.__AETHER_TEST__.start();
  }, workServerId);

  for (let i = 0; i < 40; i++) {
    await page.waitForTimeout(2000);
    const s = await page.evaluate(() => ({
      state: window.__AETHER_TEST__.getState(),
      uid: window.__AETHER_TEST__.getUid(),
    }));
    console.log('T+' + i * 2 + 's', JSON.stringify(s));
    if (String(s.state).startsWith('Error')) {
      break;
    }
    if (pingReceived) {
      break;
    }
    if (
      (String(s.state).startsWith('Ready') ||
        String(s.state).startsWith('Connected')) &&
      i >= 25
    ) {
      break;
    }
  }

  const summary = {
    pingReceived,
    pingTimeout,
    workOpen,
    wsRecvOn9023: wsRecvAfterLogin,
    loginEncrypted: logs.filter((l) => /Login api encrypted size/i.test(l))
      .length,
    pingSend: logs.filter((l) => /Ping server id/i.test(l)).length,
  };
  console.log('SUMMARY', JSON.stringify(summary));
  await browser.close();
  if (!pingReceived) {
    process.exit(2);
  }
})().catch((e) => {
  console.error(e);
  process.exit(1);
});

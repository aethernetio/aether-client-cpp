// @ts-check
const { test, expect } = require('@playwright/test');

/**
 * Two tabs of the same profile: the second must fail to acquire the lock.
 */

test('second tab cannot acquire same profile lock @lock', async ({ browser }) => {
  const gateway =
    process.env.AETHER_BPP_GATEWAY || 'wss://dbservice.aethernet.io:9013/';
  const transport = (process.env.AETHER_BPP_TRANSPORT || 'WSS').toUpperCase();

  const context1 = await browser.newContext();
  const page1 = await context1.newPage();
  const params = new URLSearchParams({
    profile: 'LOCKTEST',
    transport,
    gateway,
  });
  await page1.goto(`/?${params.toString()}`);
  await page1.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  await page1.evaluate(() => window.__AETHER_TEST__.start());
  await page1.waitForFunction(() => {
    const s = window.__AETHER_TEST__.getState();
    return (
      s.startsWith('Ready') ||
      s.startsWith('Connected') ||
      s.startsWith('Loading') ||
      s.startsWith('Select') ||
      s.startsWith('Construct')
    );
  }, null, { timeout: 120_000 });

  const context2 = await browser.newContext();
  const page2 = await context2.newPage();
  await page2.goto(`/?${params.toString()}`);
  await page2.waitForFunction(() => !!window.__AETHER_TEST__, null, {
    timeout: 120_000,
  });
  await page2.evaluate(() => window.__AETHER_TEST__.start());
  await page2.waitForFunction(() => {
    const s = window.__AETHER_TEST__.getState();
    return s.startsWith('Error');
  }, null, { timeout: 60_000 });

  const state2 = await page2.evaluate(() => window.__AETHER_TEST__.getState());
  expect(state2.toLowerCase()).toContain('lock');

  await context2.close();
  await context1.close();
});

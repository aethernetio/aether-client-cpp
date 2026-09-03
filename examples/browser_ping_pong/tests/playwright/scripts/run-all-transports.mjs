#!/usr/bin/env node
/**
 * Runs Playwright once per transport tag.
 * HTTPS/WSS are skipped when AETHER_BPP_SKIP_TLS=1 or AETHER_BPP_TLS_READY is unset.
 */
import { spawnSync } from 'node:child_process';

const skipTls =
  process.env.AETHER_BPP_SKIP_TLS === '1' ||
  process.env.AETHER_BPP_SKIP_TLS === 'true' ||
  !process.env.AETHER_BPP_TLS_READY;

const transports = [
  { tag: '@ws', env: {} },
  { tag: '@http', env: {} },
  {
    tag: '@wss',
    env: {},
    optionalTls: true,
  },
  {
    tag: '@https',
    env: {},
    optionalTls: true,
  },
];

let failed = false;
for (const t of transports) {
  if (t.optionalTls && skipTls) {
    console.log(`skip ${t.tag} (TLS not ready)`);
    continue;
  }
  console.log(`run ${t.tag}`);
  const r = spawnSync(
    process.platform === 'win32' ? 'npx.cmd' : 'npx',
    ['playwright', 'test', '--grep', t.tag],
    {
      stdio: 'inherit',
      env: { ...process.env, ...t.env },
      shell: process.platform === 'win32',
    },
  );
  if (r.status !== 0) {
    failed = true;
  }
}

process.exit(failed ? 1 : 0);

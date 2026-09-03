# Browser ping-pong Playwright e2e

Serve the CMake post-build `web/` directory, start the browser transport
gateway, then:

```bash
cd examples/browser_ping_pong/tests/playwright
npm install
npx playwright install chromium
export AETHER_BPP_BASE_URL=http://127.0.0.1:4173
export AETHER_BPP_GATEWAY=ws://127.0.0.1:8080
# Optional TLS:
# export AETHER_BPP_TLS_READY=1
# export AETHER_BPP_TRUST_LOCAL_CA=1
# Without certs, leave TLS skipped (default):
export AETHER_BPP_SKIP_TLS=1
npm run test:all-transports
```

Assertions use `window.__AETHER_TEST__` and DOM `data-state` / `data-stat`
attributes — not screenshots.

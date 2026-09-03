# Browser ping-pong Playwright e2e

## Gateway smoke (test-only)

Serve the CMake post-build `web/` directory, start the browser transport
gateway, then:

```bash
cd examples/browser_ping_pong/tests/playwright
npm install
npx playwright install chromium
export AETHER_BPP_BASE_URL=http://127.0.0.1:4173
export AETHER_BPP_GATEWAY=ws://127.0.0.1:8080/aether/v1/ws
export AETHER_BPP_SKIP_TLS=1
npm run test:all-transports
```

Gateway success is **not** proof of live Æther connectivity.

## Live cloud

```bash
export AETHER_BPP_BASE_URL=http://127.0.0.1:4173
export AETHER_BPP_LIVE=1
export AETHER_BPP_GATEWAY=wss://dbservice.aethernet.io:9013/
export AETHER_BPP_TRANSPORT=WSS
export AETHER_BPP_PING_COUNT=100
npm run test:live
npm run test:lock
```

`test:live` fails if `AETHER_BPP_GATEWAY` looks like the test-only gateway
(`127.0.0.1`, `localhost`, or `/aether/v1`).

Assertions use `window.__AETHER_TEST__` and DOM `data-state` / `data-stat`
attributes — not screenshots.

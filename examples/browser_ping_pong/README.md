# Browser Aether Ping/Pong

Static Emscripten example: WASM Aether client + HTML/JS UI (no parallel JS protocol).

## Build

```bash
# after emsdk activate
emcmake cmake -S . -B build/emscripten-browser \
  -DAE_BUILD_EXAMPLES=ON \
  -DAE_BUILD_BROWSER_EXAMPLE=ON \
  -DAE_BUILD_TESTS=OFF \
  -DAE_BUILD_TOOLS=OFF \
  -DAE_DISTILLATION=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/emscripten-browser --target aether_browser_ping_pong
```

Static output:

`build/emscripten-browser/examples/browser_ping_pong/web/`

Serve that directory over HTTP(S). Open `?profile=A` and `?profile=B` in two tabs.

## Vendor crypto

See `vendor/README.md`. After packaging libsodium.js 0.8.4 into `vendor/libsodium/`,
the post-build step copies it into `web/vendor/libsodium/`.

## E2E

See `tests/playwright/README.md`.

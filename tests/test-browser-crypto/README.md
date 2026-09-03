# Browser crypto known-answer tests

## Native (CTest)

Configured when `AE_BUILD_TESTS=ON` (desktop builds; not under Emscripten):

```bash
cmake --build <build_dir> --target test-browser-crypto
ctest --test-dir <build_dir> -R test-browser-crypto --output-on-failure
# or:
./<build_dir>/tests/run/test-browser-crypto
```

Vectors: `vectors.json` (ChaCha20-Poly1305 original layout, KDF, Ed25519, box_seal open, bcrypt+crc32).

## JS (libsodium.js 0.8.4)

```bash
cd examples/browser_ping_pong/vendor
npm install
npm run vendor
cd ../../..
node tests/test-browser-crypto/run_js_kats.js
```

Requires Node.js. Loads `libsodium-wrappers@0.8.4` from the vendor `node_modules`.

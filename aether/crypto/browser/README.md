# Browser crypto (Emscripten)

## Choice: JS sodium ops + C headers / transitional C link

Under `__EMSCRIPTEN__`:

| Concern | Implementation |
|---------|----------------|
| ChaCha20-Poly1305 **original**, box_seal, KDF, Ed25519 verify, random | **libsodium.js 0.8.4** via `browser_sodium_bridge` (`Module.aeSodium`) |
| Size constants (`NPUBBYTES`, `ABYTES`, …) | `sodium.h` from CPM libsodium |
| Hydrogen | **Not linked** |
| bcrypt PoW | CPM **libbcrypt** into WASM by default; optional `Module.aeBcrypt` JS after KATs |

CPM `sodium` may still be linked into the WASM binary for headers and any
residual C call sites during the milestone transition. Browser-facing providers
(`#if defined(__EMSCRIPTEN__)`) call the JS bridge exclusively for the ops
above. A later cleanup may switch sodium to headers-only.

Vendored scripts: `examples/browser_ping_pong/vendor/` (no CDN).

/**
 * Copyright 2026 Aethernet Inc.
 *
 * Optional JS bcrypt backend for browser PoW. Default Emscripten builds use
 * libbcrypt compiled into WASM; install Module.aeBcrypt only after KAT pass.
 *
 * Link optional: --js-library aether/crypto/browser/browser_bcrypt.js
 * (not required when using WASM libbcrypt only).
 */

mergeInto(LibraryManager.library, {
  ae_browser_bcrypt_js_library_present: function () {
    return 1;
  },
});

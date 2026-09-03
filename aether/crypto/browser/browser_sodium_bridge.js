/**
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Emscripten JS library bridge to vendored libsodium.js 0.8.4.
 *
 * Page must set Module.aeSodium = sodium after `await sodium.ready`.
 * Uses ORIGINAL crypto_aead_chacha20poly1305 (not IETF / not XChaCha20).
 *
 * Link with: --js-library aether/crypto/browser/browser_sodium_bridge.js
 */

mergeInto(LibraryManager.library, {
  ae_browser_sodium_is_ready: function () {
    var s = (typeof Module !== 'undefined' && Module.aeSodium) ? Module.aeSodium : null;
    return (s && s.ready !== undefined) ? 1 : (s ? 1 : 0);
  },

  ae_browser_sodium_randombytes: function (out_ptr, len) {
    var s = Module.aeSodium;
    if (!s) {
      throw 'aeSodium not ready';
    }
    var bytes = s.randombytes_buf(len);
    HEAPU8.set(bytes, out_ptr);
  },

  ae_browser_sodium_aead_keygen: function (key_ptr) {
    var s = Module.aeSodium;
    var key = s.crypto_aead_chacha20poly1305_keygen();
    HEAPU8.set(key, key_ptr);
  },

  /**
   * Original ChaCha20-Poly1305 encrypt (8-byte nonce).
   * Writes ciphertext||mac to c_ptr; stores length at clen_ptr (ull).
   * Returns 0 on success, -1 on failure.
   */
  ae_browser_sodium_aead_encrypt: function (c_ptr, clen_ptr, m_ptr, mlen,
                                            n_ptr, k_ptr) {
    var s = Module.aeSodium;
    try {
      var m = new Uint8Array(HEAPU8.subarray(m_ptr, m_ptr + mlen));
      var n = new Uint8Array(HEAPU8.subarray(n_ptr, n_ptr + 8));
      var k = new Uint8Array(HEAPU8.subarray(k_ptr, k_ptr + 32));
      // libsodium.js: additional data optional; nsec unused for this construct.
      var c = s.crypto_aead_chacha20poly1305_encrypt(m, null, null, n, k);
      HEAPU8.set(c, c_ptr);
      // write unsigned long long little-endian
      var lo = c.length >>> 0;
      var hi = (c.length / 0x100000000) >>> 0;
      HEAPU32[(clen_ptr >> 2)] = lo;
      HEAPU32[(clen_ptr >> 2) + 1] = hi;
      return 0;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_sodium_aead_decrypt: function (m_ptr, mlen_ptr, c_ptr, clen,
                                            n_ptr, k_ptr) {
    var s = Module.aeSodium;
    try {
      var c = new Uint8Array(HEAPU8.subarray(c_ptr, c_ptr + clen));
      var n = new Uint8Array(HEAPU8.subarray(n_ptr, n_ptr + 8));
      var k = new Uint8Array(HEAPU8.subarray(k_ptr, k_ptr + 32));
      var m = s.crypto_aead_chacha20poly1305_decrypt(null, c, null, n, k);
      HEAPU8.set(m, m_ptr);
      var lo = m.length >>> 0;
      var hi = (m.length / 0x100000000) >>> 0;
      HEAPU32[(mlen_ptr >> 2)] = lo;
      HEAPU32[(mlen_ptr >> 2) + 1] = hi;
      return 0;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_sodium_box_seal: function (c_ptr, m_ptr, mlen, pk_ptr) {
    var s = Module.aeSodium;
    try {
      var m = new Uint8Array(HEAPU8.subarray(m_ptr, m_ptr + mlen));
      var pk = new Uint8Array(HEAPU8.subarray(pk_ptr, pk_ptr + 32));
      var c = s.crypto_box_seal(m, pk);
      HEAPU8.set(c, c_ptr);
      return 0;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_sodium_box_seal_open: function (m_ptr, c_ptr, clen, pk_ptr,
                                             sk_ptr) {
    var s = Module.aeSodium;
    try {
      var c = new Uint8Array(HEAPU8.subarray(c_ptr, c_ptr + clen));
      var pk = new Uint8Array(HEAPU8.subarray(pk_ptr, pk_ptr + 32));
      var sk = new Uint8Array(HEAPU8.subarray(sk_ptr, sk_ptr + 32));
      var m = s.crypto_box_seal_open(c, pk, sk);
      HEAPU8.set(m, m_ptr);
      return 0;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_sodium_kdf_derive_from_key: function (subkey_ptr, subkey_len,
                                                   subkey_id_lo, subkey_id_hi,
                                                   ctx_ptr, key_ptr) {
    var s = Module.aeSodium;
    try {
      // libsodium.js 0.8.4 requires bigint for full uint64 subkey_id.
      var subkey_id = BigInt(subkey_id_lo >>> 0) +
                      (BigInt(subkey_id_hi >>> 0) * BigInt(0x100000000));
      var ctx = UTF8ToString(ctx_ptr, 8);
      // Ensure exactly 8 bytes for sodium KDF context.
      if (ctx.length < 8) {
        ctx = (ctx + "________").substring(0, 8);
      } else if (ctx.length > 8) {
        ctx = ctx.substring(0, 8);
      }
      var key = new Uint8Array(HEAPU8.subarray(key_ptr, key_ptr + 32));
      var sub = s.crypto_kdf_derive_from_key(subkey_len, subkey_id, ctx, key);
      HEAPU8.set(sub, subkey_ptr);
      return 0;
    } catch (e) {
      return -1;
    }
  },

  ae_browser_sodium_sign_verify_detached: function (sig_ptr, m_ptr, mlen,
                                                    pk_ptr) {
    var s = Module.aeSodium;
    if (!s) {
      return -1;
    }
    try {
      // Copy out of the Emscripten heap: libsodium.js WASM cannot consume
      // TypedArray views backed by a foreign ArrayBuffer.
      var sig = new Uint8Array(HEAPU8.subarray(sig_ptr, sig_ptr + 64));
      var m = new Uint8Array(HEAPU8.subarray(m_ptr, m_ptr + mlen));
      var pk = new Uint8Array(HEAPU8.subarray(pk_ptr, pk_ptr + 32));
      var ok = s.crypto_sign_verify_detached(sig, m, pk);
      return ok ? 0 : -1;
    } catch (e) {
      console.error("ae_browser_sodium_sign_verify_detached", e);
      return -1;
    }
  },
});

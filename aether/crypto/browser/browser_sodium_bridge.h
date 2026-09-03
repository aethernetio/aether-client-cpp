/*
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

#ifndef AETHER_CRYPTO_BROWSER_BROWSER_SODIUM_BRIDGE_H_
#define AETHER_CRYPTO_BROWSER_BROWSER_SODIUM_BRIDGE_H_

#if defined(__EMSCRIPTEN__)

#  include <cstddef>
#  include <cstdint>

namespace ae::browser_sodium {

/**
 * Narrow JS↔C++ bridge to vendored libsodium.js (0.8.4).
 *
 * Requires Module.aeSodium = sodium after `await sodium.ready`.
 * Uses original crypto_aead_chacha20poly1305 (8-byte nonce), not IETF.
 *
 * Sodium size constants still come from CPM sodium.h headers; runtime ops
 * go through this bridge under __EMSCRIPTEN__.
 */

bool IsReady();

void RandomBytes(std::uint8_t* out, std::size_t len);

/** crypto_aead_chacha20poly1305_keygen */
void AeadKeygen(std::uint8_t* key /* KEYBYTES */);

/**
 * Original AEAD encrypt. Writes ciphertext||tag into |c|; |clen| is set to
 * ciphertext length (not including any caller-appended nonce).
 * Returns 0 on success.
 */
int AeadEncrypt(std::uint8_t* c, unsigned long long* clen,
                std::uint8_t const* m, unsigned long long mlen,
                std::uint8_t const* n /* NPUBBYTES */,
                std::uint8_t const* k /* KEYBYTES */);

/** Original AEAD decrypt. Returns 0 on success. */
int AeadDecrypt(std::uint8_t* m, unsigned long long* mlen,
                std::uint8_t const* c, unsigned long long clen,
                std::uint8_t const* n /* NPUBBYTES */,
                std::uint8_t const* k /* KEYBYTES */);

int BoxSeal(std::uint8_t* c, std::uint8_t const* m, unsigned long long mlen,
            std::uint8_t const* pk);

int BoxSealOpen(std::uint8_t* m, std::uint8_t const* c, unsigned long long clen,
                std::uint8_t const* pk, std::uint8_t const* sk);

int KdfDeriveFromKey(std::uint8_t* subkey, std::size_t subkey_len,
                     std::uint64_t subkey_id, char const* ctx /* 8 bytes */,
                     std::uint8_t const* key);

int SignVerifyDetached(std::uint8_t const* sig, std::uint8_t const* m,
                       unsigned long long mlen, std::uint8_t const* pk);

}  // namespace ae::browser_sodium

#endif  // defined(__EMSCRIPTEN__)
#endif  // AETHER_CRYPTO_BROWSER_BROWSER_SODIUM_BRIDGE_H_

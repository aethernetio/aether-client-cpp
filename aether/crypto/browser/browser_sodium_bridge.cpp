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

#include "aether/crypto/browser/browser_sodium_bridge.h"

#if defined(__EMSCRIPTEN__)

#  include <cassert>
#  include <cstring>

extern "C" {

int ae_browser_sodium_is_ready(void);
void ae_browser_sodium_randombytes(std::uint8_t* out, std::size_t len);
void ae_browser_sodium_aead_keygen(std::uint8_t* key);
int ae_browser_sodium_aead_encrypt(std::uint8_t* c, unsigned long long* clen,
                                   std::uint8_t const* m,
                                   unsigned long long mlen,
                                   std::uint8_t const* n,
                                   std::uint8_t const* k);
int ae_browser_sodium_aead_decrypt(std::uint8_t* m, unsigned long long* mlen,
                                   std::uint8_t const* c,
                                   unsigned long long clen,
                                   std::uint8_t const* n,
                                   std::uint8_t const* k);
int ae_browser_sodium_box_seal(std::uint8_t* c, std::uint8_t const* m,
                               unsigned long long mlen, std::uint8_t const* pk);
int ae_browser_sodium_box_seal_open(std::uint8_t* m, std::uint8_t const* c,
                                    unsigned long long clen,
                                    std::uint8_t const* pk,
                                    std::uint8_t const* sk);
int ae_browser_sodium_kdf_derive_from_key(std::uint8_t* subkey,
                                          std::size_t subkey_len,
                                          std::uint32_t subkey_id_lo,
                                          std::uint32_t subkey_id_hi,
                                          char const* ctx,
                                          std::uint8_t const* key);
int ae_browser_sodium_sign_verify_detached(std::uint8_t const* sig,
                                           std::uint8_t const* m,
                                           unsigned long long mlen,
                                           std::uint8_t const* pk);

}  // extern "C"

namespace ae::browser_sodium {

bool IsReady() { return ae_browser_sodium_is_ready() != 0; }

void RandomBytes(std::uint8_t* out, std::size_t len) {
  assert(out != nullptr || len == 0);
  if (len == 0) {
    return;
  }
  ae_browser_sodium_randombytes(out, len);
}

void AeadKeygen(std::uint8_t* key) {
  assert(key != nullptr);
  ae_browser_sodium_aead_keygen(key);
}

int AeadEncrypt(std::uint8_t* c, unsigned long long* clen,
                std::uint8_t const* m, unsigned long long mlen,
                std::uint8_t const* n, std::uint8_t const* k) {
  return ae_browser_sodium_aead_encrypt(c, clen, m, mlen, n, k);
}

int AeadDecrypt(std::uint8_t* m, unsigned long long* mlen,
                std::uint8_t const* c, unsigned long long clen,
                std::uint8_t const* n, std::uint8_t const* k) {
  return ae_browser_sodium_aead_decrypt(m, mlen, c, clen, n, k);
}

int BoxSeal(std::uint8_t* c, std::uint8_t const* m, unsigned long long mlen,
            std::uint8_t const* pk) {
  return ae_browser_sodium_box_seal(c, m, mlen, pk);
}

int BoxSealOpen(std::uint8_t* m, std::uint8_t const* c, unsigned long long clen,
                std::uint8_t const* pk, std::uint8_t const* sk) {
  return ae_browser_sodium_box_seal_open(m, c, clen, pk, sk);
}

int KdfDeriveFromKey(std::uint8_t* subkey, std::size_t subkey_len,
                     std::uint64_t subkey_id, char const* ctx,
                     std::uint8_t const* key) {
  auto const lo = static_cast<std::uint32_t>(subkey_id & 0xffffffffu);
  auto const hi = static_cast<std::uint32_t>((subkey_id >> 32) & 0xffffffffu);
  return ae_browser_sodium_kdf_derive_from_key(subkey, subkey_len, lo, hi, ctx,
                                               key);
}

int SignVerifyDetached(std::uint8_t const* sig, std::uint8_t const* m,
                       unsigned long long mlen, std::uint8_t const* pk) {
  return ae_browser_sodium_sign_verify_detached(sig, m, mlen, pk);
}

}  // namespace ae::browser_sodium

#endif  // defined(__EMSCRIPTEN__)

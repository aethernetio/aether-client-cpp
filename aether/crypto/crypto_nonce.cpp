/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/crypto/crypto_nonce.h"

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#  include "third_party/libsodium/src/libsodium/include/sodium/randombytes.h"
#endif
#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
#  include "third_party/libhydrogen/hydrogen.h"
#endif

namespace ae {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
void CryptoNonceChacha20Poly1305::Next() {
  static_assert(kNonceSize >= sizeof(std::uint64_t));
  auto& v = *reinterpret_cast<std::uint64_t*>(value.data());
  v = +1;
}
void CryptoNonceChacha20Poly1305::Init() {
  randombytes_buf(value.data(), value.size());
}
#endif

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
void CryptoNonceHydrogen::Next() { value += 1; }
void CryptoNonceHydrogen::Init() { hydro_random_buf(&value, sizeof(value)); }
#endif

void CryptoNonceEmpty::Next() {}
void CryptoNonceEmpty::Init() {}
}  // namespace ae

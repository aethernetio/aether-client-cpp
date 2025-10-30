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

#ifndef AETHER_CRYPTO_CRYPTO_NONCE_H_
#define AETHER_CRYPTO_CRYPTO_NONCE_H_

#include <array>
#include <cstdint>

#include "aether/config.h"
#include "aether/reflect/reflect.h"

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
// too long include string
#  include "third_party/libsodium/src/libsodium/include/sodium/\
crypto_aead_chacha20poly1305.h"  // " this helps ide to parse following quotes

#endif

namespace ae {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
static constexpr auto kNonceSize = crypto_aead_chacha20poly1305_NPUBBYTES;
struct CryptoNonceChacha20Poly1305 {
  void Next();
  void Init();

  AE_REFLECT_MEMBERS(value)

  std::array<std::uint8_t, kNonceSize> value;
};
#endif

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
struct CryptoNonceHydrogen {
  void Next();
  void Init();

  AE_REFLECT_MEMBERS(value)

  std::uint64_t value;
};
#endif

struct CryptoNonceEmpty {
  void Next();
  void Init();

  AE_REFLECT()
};

struct CryptoNonce : public
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
                     CryptoNonceChacha20Poly1305
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
                     CryptoNonceHydrogen
#else
                     CryptoNonceEmpty
#endif
{
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  AE_REFLECT(AE_REF_BASE(CryptoNonceChacha20Poly1305))
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  AE_REFLECT(AE_REF_BASE(CryptoNonceHydrogen))
#else
  AE_REFLECT(AE_REF_BASE(CryptoNonceEmpty))
#endif
};
}  // namespace ae

#endif  // AETHER_CRYPTO_CRYPTO_NONCE_H_

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

#ifndef AETHER_CRYPTO_CRYPTO_DEFINITIONS_H_
#define AETHER_CRYPTO_CRYPTO_DEFINITIONS_H_

#include <cstdint>

#include "aether/config.h"

#if AE_SIGNATURE == AE_HYDRO_SIGNATURE || AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
#  define HYDRO_CONTEXT "aetherio"
#endif

// 8 bytes length
#if AE_KDF == AE_SODIUM_KDF
#  define SODIUM_KDF_CONTEXT "_aether_"
#elif AE_KDF == AE_HYDRO_KDF
#  define HYDRO_KDF_CONTEXT "_aether_"
#endif

namespace ae {
// Cryptographic profiles
enum class CryptoLib : std::uint8_t {
  kSodiumLib = 0,  // use sodium library only
  kHydrogenLib,    // use hydrogen library only
  // TODO: add different ASYNC/SYNC combinations
};

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 && \
    AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
inline constexpr auto kDefaultCryptoLibProfile = CryptoLib::kSodiumLib;
#endif

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK && \
    AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
inline constexpr auto kDefaultCryptoLibProfile = CryptoLib::kHydrogenLib;
#endif

#if CRYPTO_HASH_USED
// WARNING: unused
enum class CryptoHashMethod : std::uint8_t {
  kNone = AE_NONE,
#  if AE_CRYPTO_HASH == AE_BLAKE2B
  kBlake2b = AE_BLAKE2B,
#  elif AE_CRYPT_HASH == AE_HYDRO_HASH
  kHydroHash = AE_HYDRO_HASH,
#  endif  // AE_CRYPTO_HASH == AE_BLAKE2B
};
#endif

enum class PowMethod : std::uint8_t {
  kBCryptCrc32 = 0,
};

}  // namespace ae

#endif  // AETHER_CRYPTO_CRYPTO_DEFINITIONS_H_

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

#ifndef AETHER_CRYPTO_KEY_H_
#define AETHER_CRYPTO_KEY_H_

#include "aether/config.h"

#if AE_CRYPTO_SYNC != AE_NONE || AE_CRYPTO_ASYNC != AE_NONE || \
    AE_SIGNATURE != AE_NONE

#  include <array>
#  include <string>
#  include <cstdint>
#  include <cassert>
#  include <sstream>
#  include <iomanip>

#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
#    include "third_party/libsodium/src/libsodium/include/sodium/crypto_box.h"
#  endif
#  if AE_SIGNATURE == AE_ED25519
#    include "third_party/libsodium/src/libsodium/include/sodium/crypto_sign.h"
#  endif

#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#    include "third_party/libsodium/src/libsodium/include/sodium/\
crypto_aead_chacha20poly1305.h"  //"
#  endif

#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK ||  \
      AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK || \
      AE_SIGNATURE == AE_HYDRO_SIGNATURE
#    include "third_party/libhydrogen/hydrogen.h"
#  endif

#  include "aether/reflect/reflect.h"
#  include "aether/types/variant_type.h"

namespace ae {

enum class CryptoKeyType : std::uint8_t {
  kHydrogenCurvePrivate = 1,
  kHydrogenCurvePublic = 2,
  kHydrogenSecretBox = 3,
  kHydrogenSignPrivate = 4,
  kHydrogenSignPublic = 5,
  kSodiumChacha20Poly1305 = 6,
  kSodiumCurvePrivate = 7,
  kSodiumCurvePublic = 8,
  kSodiumSignPrivate = 9,
  kSodiumSignPublic = 10,
  kLast,
};

#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
struct SodiumCurvePublicKey {
  AE_REFLECT_MEMBERS(key)

  std::array<std::uint8_t, crypto_box_PUBLICKEYBYTES> key;
};

struct SodiumCurvePrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_box_SECRETKEYBYTES> key;
};
#  endif

#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
struct SodiumChacha20Poly1305Key {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_aead_chacha20poly1305_KEYBYTES> key;
};
#  endif

#  if AE_SIGNATURE == AE_ED25519
struct SodiumSignPublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> key;
};

struct SodiumSignPrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> key;
};
#  endif

#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
struct HydrogenCurvePublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_kx_PUBLICKEYBYTES> key;
};

struct HydrogenCurvePrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_kx_SECRETKEYBYTES> key;
};
#  endif

#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
struct HydrogenSecretBoxKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_secretbox_KEYBYTES> key;
};
#  endif

#  if AE_SIGNATURE == AE_HYDRO_SIGNATURE
struct HydrogenSignPublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_sign_PUBLICKEYBYTES> key;
};

struct HydrogenSignPrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_sign_SECRETKEYBYTES> key;
};
#  endif

struct BlankKey {
  std::array<std::uint8_t, 0> key;

  AE_REFLECT_MEMBERS(key)
};

class Key
    : public VariantType<
          CryptoKeyType,
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
          VPair<CryptoKeyType::kSodiumChacha20Poly1305,
                SodiumChacha20Poly1305Key>,
#  endif
#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
          VPair<CryptoKeyType::kHydrogenCurvePrivate, HydrogenCurvePrivateKey>,
          VPair<CryptoKeyType::kHydrogenCurvePublic, HydrogenCurvePublicKey>,
#  endif
#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
          VPair<CryptoKeyType::kHydrogenSecretBox, HydrogenSecretBoxKey>,
#  endif
#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
          VPair<CryptoKeyType::kSodiumCurvePublic, SodiumCurvePublicKey>,
          VPair<CryptoKeyType::kSodiumCurvePrivate, SodiumCurvePrivateKey>,
#  endif
#  if AE_SIGNATURE == AE_ED25519
          VPair<CryptoKeyType::kSodiumSignPrivate, SodiumSignPrivateKey>,
          VPair<CryptoKeyType::kSodiumSignPublic, SodiumSignPublicKey>,
#  endif
#  if AE_SIGNATURE == AE_HYDRO_SIGNATURE
          VPair<CryptoKeyType::kHydrogenSignPrivate, HydrogenSignPrivateKey>,
          VPair<CryptoKeyType::kHydrogenSignPublic, HydrogenSignPublicKey>,
#  endif
          VPair<CryptoKeyType::kLast, BlankKey>
          // leave it here to make sure type list ended without comma
          > {
 public:
  using VariantType::VariantType;

  std::uint8_t const* Data() const {
    return std::visit([](auto const& v) { return v.key.data(); },
                      static_cast<typename VariantType::variant const&>(*this));
  }

  std::size_t Size() const {
    return std::visit([](auto const& v) { return v.key.size(); },
                      static_cast<typename VariantType::variant const&>(*this));
  }

  static std::string text(Key const& v) {
    std::stringstream ss;
    ss << "[";
    std::visit(
        [&ss](auto const& k) {
          for (auto s : k.key) {
            ss << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<std::uint32_t>(s);
          }
        },
        static_cast<typename VariantType::variant const&>(v));
    ss << "]";
    return ss.str();
  }
};

}  // namespace ae
#endif
#endif  // AETHER_CRYPTO_KEY_H_

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

#  include <cstdint>
#  include <array>
#  include <cassert>
#  include <sstream>
#  include <string>

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

#  include "aether/variant_type.h"
#  include "aether/reflect/reflect.h"

namespace ae {

enum class CryptoKeyType : std::uint8_t {
  kSodiumCurvePublic,
  kSodiumCurveSecret,
  kSodiumChacha,
  kSodiumSignPublic,
  kSodiumSignSecret,
  kHydrogenSignPublic,
  kHydrogenSignPrivate,
  kHydrogenCurvePublic,
  kHydrogenCurveSecret,
  kHydrogenSecretBox,
};

#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
struct SodiumCurvePublicKey {
  AE_REFLECT_MEMBERS(key)

  std::array<std::uint8_t, crypto_box_PUBLICKEYBYTES> key;
};

struct SodiumCurveSecretKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_box_SECRETKEYBYTES> key;
};
#  endif

#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
struct SodiumChachaKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_aead_chacha20poly1305_KEYBYTES> key;
};
#  endif

#  if AE_SIGNATURE == AE_ED25519
struct SodiumSignPublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> key;
};

struct SodiumSignSecretKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> key;
};
#  endif

#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
struct HydrogenCurvePublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_kx_PUBLICKEYBYTES> key;
};

struct HydrogenCurveSecretKey {
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

struct HydrogenSignSecretKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, hydro_sign_SECRETKEYBYTES> key;
};
#  endif

struct BlankKey {
  std::array<std::uint8_t, 0> key;

  AE_REFLECT_MEMBERS(key)
};

class Key : public VariantType<CryptoKeyType,
#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
                               SodiumCurvePublicKey, SodiumCurveSecretKey,
#  endif
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305

                               SodiumChachaKey,
#  endif
#  if AE_SIGNATURE == AE_ED25519

                               SodiumSignPublicKey, SodiumSignSecretKey,
#  endif
#  if AE_SIGNATURE == AE_HYDRO_SIGNATURE
                               HydrogenSignPublicKey, HydrogenSignSecretKey,
#  endif
#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
                               HydrogenCurvePublicKey, HydrogenCurveSecretKey,
#  endif
#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
                               HydrogenSecretBoxKey,
#  endif
                               BlankKey  // leave it here to make sure type list
                                         // ended without comma
                               > {
 private:
  static constexpr std::size_t kSodiumCurveOffset = 0;
  static constexpr std::size_t kChachaOffset =
      kSodiumCurveOffset + ((AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL) ? 2 : 0);
  static constexpr std::size_t kSodiumSignOffset =
      kChachaOffset + ((AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305) ? 1 : 0);
  static constexpr std::size_t kHydrogenSignOffset =
      kChachaOffset + ((AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305) ? 1 : 0);
  static constexpr std::size_t kHydrogenCurveOffset =
      kHydrogenSignOffset + ((AE_SIGNATURE != AE_NONE) ? 2 : 0);
  static constexpr std::size_t kHydrogenSecretBoxOffset =
      kHydrogenCurveOffset + (AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK ? 2 : 0);

 public:
  using VariantType::VariantType;

  // Conversion from index type to variant type order
  std::size_t IndexToOrder(index_type index) const override {
    switch (index) {
#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
      case CryptoKeyType::kSodiumCurvePublic:
      case CryptoKeyType::kSodiumCurveSecret: {
        return kSodiumCurveOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kSodiumCurvePublic);
      }
#  endif
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
      case CryptoKeyType::kSodiumChacha: {
        return kChachaOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kSodiumChacha);
      }
#  endif
#  if AE_SIGNATURE == AE_ED25519
      case CryptoKeyType::kSodiumSignPublic:
      case CryptoKeyType::kSodiumSignSecret: {
        return kSodiumSignOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kSodiumSignPublic);
      }
#  endif
#  if AE_SIGNATURE == AE_HYDRO_SIGNATURE
      case CryptoKeyType::kHydrogenSignPublic:
      case CryptoKeyType::kHydrogenSignPrivate: {
        return kHydrogenSignOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kHydrogenSignPublic);
      }
#  endif
#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
      case CryptoKeyType::kHydrogenCurvePublic:
      case CryptoKeyType::kHydrogenCurveSecret: {
        return kHydrogenCurveOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kHydrogenCurvePublic);
      }
#  endif
#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
      case CryptoKeyType::kHydrogenSecretBox:
        return kHydrogenSecretBoxOffset + static_cast<std::size_t>(index) -
               static_cast<std::size_t>(CryptoKeyType::kHydrogenSecretBox);
#  endif
      default:
        assert(false);
    }
    return {};
  }

  // Conversion from variant type order to index type
  index_type OrderToIndex(std::size_t order) const override {
#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
    if (order < kSodiumCurveOffset + 2) {
      return static_cast<index_type>(
          static_cast<std::size_t>(CryptoKeyType::kSodiumCurvePublic) +
          (order - kSodiumCurveOffset));
    }
#  endif
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
    if (order == kChachaOffset) {
      return CryptoKeyType::kSodiumChacha;
    }
#  endif
#  if AE_SIGNATURE == AE_ED25519
    if (order < kSodiumSignOffset + 2) {
      return static_cast<index_type>(
          static_cast<std::size_t>(CryptoKeyType::kSodiumSignPublic) +
          (order - kSodiumSignOffset));
    }
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    if (order < kHydrogenSignOffset + 2) {
      return static_cast<index_type>(
          static_cast<std::size_t>(CryptoKeyType::kHydrogenSignPublic) +
          (order - kHydrogenSignOffset));
    }
#  endif

#  if AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK
    if (order < kHydrogenCurveOffset + 2) {
      return static_cast<index_type>(
          static_cast<std::size_t>(CryptoKeyType::kHydrogenCurvePublic) +
          (order - kHydrogenCurveOffset));
    }
#  endif
#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
    if (order == kHydrogenSecretBoxOffset) {
      return CryptoKeyType::kHydrogenSecretBox;
    }
#  endif
    // no supported type found
    assert(false);
    return {};
  }

  std::uint8_t const* Data() const {
    return std::visit([](auto const& v) { return v.key.data(); },
                      static_cast<VariantType::variant const&>(*this));
  }

  std::size_t Size() const {
    return std::visit([](auto const& v) { return v.key.size(); },
                      static_cast<VariantType::variant const&>(*this));
  }

  static std::string text(Key const& v) {
    std::stringstream ss;
    ss << "[";
    std::visit(
        [&ss](auto const& k) {
          for (auto s : k.key) {
            ss << std::hex << static_cast<std::uint32_t>(s);
          }
        },
        static_cast<VariantType::variant const&>(v));
    ss << "]";
    return ss.str();
  }
};

}  // namespace ae
#endif
#endif  // AETHER_CRYPTO_KEY_H_

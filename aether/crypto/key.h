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
#  include <span>
#  include <cstdint>
#  include <cassert>

#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
#    include <sodium/crypto_box.h>
#  endif
#  if AE_SIGNATURE == AE_ED25519
#    include <sodium/crypto_sign.h>
#  endif

#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#    include <sodium/crypto_aead_chacha20poly1305.h>
#  endif

#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK ||  \
      AE_CRYPTO_ASYNC == AE_HYDRO_CRYPTO_PK || \
      AE_SIGNATURE == AE_HYDRO_SIGNATURE
#    include <hydrogen.h>
#  endif

#  include "aether-miscpp/reflect/reflect.h"
#  include "aether-miscpp/format/format.h"
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
  kP256AesGcmSymmetric = 11,
  kP256AesGcmCurvePrivate = 12,
  kP256AesGcmCurvePublic = 13,
  kP256AesGcmSignPrivate = 14,
  kP256AesGcmSignPublic = 15,
  kLast = 16,
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


// Parse-only key containers for crypto types that are not enabled locally.
// Payloads stay opaque here, the raw bytes are preserved for pass-through.
struct HydrogenCurvePrivateKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct HydrogenCurvePublicKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct HydrogenSecretBoxKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct HydrogenSignPrivateKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 64> key;
};

struct HydrogenSignPublicKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct SodiumCurvePrivateKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct SodiumCurvePublicKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct SodiumSignPrivateKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 64> key;
};

struct SodiumSignPublicKeyView {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct P256AesGcmSymmetricKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct P256AesGcmCurvePrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct P256AesGcmCurvePublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 64> key;
};

struct P256AesGcmSignPrivateKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 32> key;
};

struct P256AesGcmSignPublicKey {
  AE_REFLECT_MEMBERS(key)
  std::array<std::uint8_t, 64> key;
};


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
#  else
          VPair<CryptoKeyType::kHydrogenCurvePrivate,
                HydrogenCurvePrivateKeyView>,
          VPair<CryptoKeyType::kHydrogenCurvePublic,
                HydrogenCurvePublicKeyView>,
#  endif
#  if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
          VPair<CryptoKeyType::kHydrogenSecretBox, HydrogenSecretBoxKey>,
#  else
          VPair<CryptoKeyType::kHydrogenSecretBox, HydrogenSecretBoxKeyView>,
#  endif
#  if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL
          VPair<CryptoKeyType::kSodiumCurvePublic, SodiumCurvePublicKey>,
          VPair<CryptoKeyType::kSodiumCurvePrivate, SodiumCurvePrivateKey>,
#  else
          VPair<CryptoKeyType::kSodiumCurvePublic, SodiumCurvePublicKeyView>,
          VPair<CryptoKeyType::kSodiumCurvePrivate, SodiumCurvePrivateKeyView>,
#  endif
#  if AE_SIGNATURE == AE_ED25519
          VPair<CryptoKeyType::kSodiumSignPrivate, SodiumSignPrivateKey>,
          VPair<CryptoKeyType::kSodiumSignPublic, SodiumSignPublicKey>,
#  else
          VPair<CryptoKeyType::kSodiumSignPrivate, SodiumSignPrivateKeyView>,
          VPair<CryptoKeyType::kSodiumSignPublic, SodiumSignPublicKeyView>,
#  endif
#  if AE_SIGNATURE == AE_HYDRO_SIGNATURE
          VPair<CryptoKeyType::kHydrogenSignPrivate, HydrogenSignPrivateKey>,
          VPair<CryptoKeyType::kHydrogenSignPublic, HydrogenSignPublicKey>,
#  else
          VPair<CryptoKeyType::kHydrogenSignPrivate, HydrogenSignPrivateKeyView>,
          VPair<CryptoKeyType::kHydrogenSignPublic, HydrogenSignPublicKeyView>,
#  endif
          VPair<CryptoKeyType::kP256AesGcmSymmetric, P256AesGcmSymmetricKey>,
          VPair<CryptoKeyType::kP256AesGcmCurvePrivate,
                P256AesGcmCurvePrivateKey>,
          VPair<CryptoKeyType::kP256AesGcmCurvePublic, P256AesGcmCurvePublicKey>,
          VPair<CryptoKeyType::kP256AesGcmSignPrivate,
                P256AesGcmSignPrivateKey>,
          VPair<CryptoKeyType::kP256AesGcmSignPublic, P256AesGcmSignPublicKey>,
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

};

template <>
struct Formatter<Key> {
  template <typename TStream>
  void Format(Key const& value, FormatContext<TStream>& ctx) const {
    auto data = std::span<std::uint8_t const>{value.Data(), value.Size()};
    ctx.out().write('[');
    Formatter<decltype(data)>{}.Format(data, ctx);
    ctx.out().write(']');
  }
};

}  // namespace ae
#endif
#endif  // AETHER_CRYPTO_KEY_H_
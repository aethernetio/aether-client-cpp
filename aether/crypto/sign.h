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

#ifndef AETHER_CRYPTO_SIGN_H_
#define AETHER_CRYPTO_SIGN_H_

#include "aether/config.h"

#if AE_SIGNATURE != AE_NONE

#  include <array>
#  include <cassert>
#  include <cstdint>
#  include <sstream>
#  include <string>

#  if AE_SIGNATURE == AE_ED25519
#    include "third_party/libsodium/src/libsodium/include/sodium/crypto_sign.h"
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
#    include "third_party/libhydrogen/hydrogen.h"
#  endif

#  include "aether/variant_type.h"
#  include "aether/reflect/reflect.h"

namespace ae {

enum class SignatureMethod : std::uint8_t {
  kEd25519 = 0,
  kHydroSignature,
};

#  if AE_SIGNATURE == AE_ED25519
inline constexpr auto kDefaultSignatureMethod = SignatureMethod::kEd25519;
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
inline constexpr auto kDefaultSignatureMethod =
    SignatureMethod::kHydroSignature;
#  endif

#  if AE_SIGNATURE == AE_ED25519
struct SignSodium {
  AE_REFLECT_MEMBERS(signature)

  std::array<uint8_t, crypto_sign_BYTES> signature;
};

#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
struct SignHydrogen {
  AE_REFLECT_MEMBERS(signature)

  std::array<uint8_t, hydro_sign_BYTES> signature;
};
#  endif

// Signature type
struct Sign : public VariantType<SignatureMethod,
#  if AE_SIGNATURE == AE_ED25519
                                 SignSodium
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
                                 SignHydrogen
#  endif
                                 > {
  using VariantType::VariantType;

  std::uint8_t const* Data() const {
    return std::visit([](auto const& v) { return v.signature.data(); },
                      static_cast<VariantType::variant const&>(*this));
  }

  std::size_t Size() const {
    return std::visit([](auto const& v) { return v.signature.size(); },
                      static_cast<VariantType::variant const&>(*this));
  }

  std::size_t IndexToOrder(index_type index) const override {
    switch (index) {
#  if AE_SIGNATURE == AE_ED25519
      case SignatureMethod::kEd25519:
        return static_cast<std::size_t>(index);
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
      case SignatureMethod::kHydroSignature:
        return static_cast<std::size_t>(index) -
               static_cast<std::size_t>(SignatureMethod::kHydroSignature);
#  endif
      default:
        assert(false);
    }
    return {};
  }
  index_type OrderToIndex(std::size_t order) const override {
#  if AE_SIGNATURE == AE_ED25519
    return static_cast<index_type>(order);
#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    return static_cast<index_type>(
        order + static_cast<std::size_t>(SignatureMethod::kHydroSignature));
#  endif
  }

  static std::string text(Sign const& v) {
    std::stringstream ss;
    ss << "[";
    std::visit(
        [&ss](auto const& k) {
          for (auto s : k.signature) {
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

#endif  // AETHER_CRYPTO_SIGN_H_

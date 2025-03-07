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

#include "aether/crypto/sodium/sodium_async_crypto_provider.h"
#include "aether/crypto/key.h"

#if AE_CRYPTO_ASYNC == AE_SODIUM_BOX_SEAL

#  include <vector>
#  include <cassert>
#  include <utility>

namespace ae {
namespace _internal {
inline std::vector<std::uint8_t> EncryptWithAsymmetric(
    SodiumCurvePublicKey const& pk, std::vector<std::uint8_t> const& raw_data) {
  auto ciphertext =
      std::vector<uint8_t>(raw_data.size() + crypto_box_SEALBYTES);

  [[maybe_unused]] auto r = crypto_box_seal(ciphertext.data(), raw_data.data(),
                                            raw_data.size(), pk.key.data());
  assert(r == 0);

  return ciphertext;
}

std::vector<std::uint8_t> DecryptWithAsymmetric(
    SodiumCurvePublicKey const& pk, SodiumCurveSecretKey const& secret_key,
    std::vector<std::uint8_t> const& encrypted_data) {
  auto decrypted_data =
      std::vector<std::uint8_t>(encrypted_data.size() - crypto_box_SEALBYTES);

  [[maybe_unused]] auto r = crypto_box_seal_open(
      decrypted_data.data(), encrypted_data.data(), encrypted_data.size(),
      pk.key.data(), secret_key.key.data());
  assert(r == 0);

  return decrypted_data;
}

}  // namespace _internal

SodiumAsyncEncryptProvider::SodiumAsyncEncryptProvider(
    std::unique_ptr<IAsyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer SodiumAsyncEncryptProvider::Encrypt(DataBuffer const& data) {
  auto pub_key = key_provider_->PublicKey();
  assert(pub_key.Index() == CryptoKeyType::kSodiumCurvePublic);

  return _internal::EncryptWithAsymmetric(pub_key.Get<SodiumCurvePublicKey>(),
                                          data);
}

std::size_t SodiumAsyncEncryptProvider::EncryptOverhead() const {
  return crypto_box_SEALBYTES;
}

SodiumAsyncDecryptProvider::SodiumAsyncDecryptProvider(
    std::unique_ptr<IAsyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer SodiumAsyncDecryptProvider::Decrypt(DataBuffer const& data) {
  auto pub_key = key_provider_->PublicKey();
  assert(pub_key.Index() == CryptoKeyType::kSodiumCurvePublic);

  auto sec_key = key_provider_->SecretKey();
  assert(sec_key.Index() == CryptoKeyType::kSodiumCurveSecret);

  return _internal::DecryptWithAsymmetric(pub_key.Get<SodiumCurvePublicKey>(),
                                          sec_key.Get<SodiumCurveSecretKey>(),
                                          data);
}

}  // namespace ae

#endif

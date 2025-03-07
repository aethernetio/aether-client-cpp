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

#include "aether/crypto/hydrogen/hydro_sync_crypto_provider.h"
#include "aether/crypto/key.h"

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK

#  include <cassert>
#  include <utility>
#  include <vector>

#  include "aether/crypto/crypto_definitions.h"

namespace ae {
namespace _internal {
inline std::vector<std::uint8_t> EncryptWithSymmetric(
    HydrogenSecretBoxKey const& secret_key,
    std::vector<std::uint8_t> const& raw_data) {
  std::vector<uint8_t> ciphertext(raw_data.size() +
                                  hydro_secretbox_HEADERBYTES);

  [[maybe_unused]] auto r = hydro_secretbox_encrypt(
      ciphertext.data(), raw_data.data(), raw_data.size(), 0, HYDRO_CONTEXT,
      secret_key.key.data());
  assert(r == 0);

  return ciphertext;
}

inline std::vector<std::uint8_t> DecryptWithSymmetric(
    HydrogenSecretBoxKey const& secret_key,
    std::vector<std::uint8_t> const& encrypted_data) {
  auto decrypted_data = std::vector<std::uint8_t>(encrypted_data.size() -
                                                  hydro_secretbox_HEADERBYTES);

  [[maybe_unused]] auto r = hydro_secretbox_decrypt(
      decrypted_data.data(), encrypted_data.data(), encrypted_data.size(), 0,
      HYDRO_CONTEXT, secret_key.key.data());
  assert(r == 0);

  return decrypted_data;
}

}  // namespace _internal

HydroSyncEncryptProvider::HydroSyncEncryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer HydroSyncEncryptProvider::Encrypt(DataBuffer const& data) {
  auto key = key_provider_->GetKey();
  assert(key.Index() == CryptoKeyType::kHydrogenSecretBox);

  return _internal::EncryptWithSymmetric(key.Get<HydrogenSecretBoxKey>(), data);
}

std::size_t HydroSyncEncryptProvider::EncryptOverhead() const {
  return hydro_secretbox_HEADERBYTES;
}

HydroSyncDecryptProvider::HydroSyncDecryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer HydroSyncDecryptProvider::Decrypt(DataBuffer const& data) {
  auto key = key_provider_->GetKey();
  assert(key.Index() == CryptoKeyType::kHydrogenSecretBox);

  return _internal::DecryptWithSymmetric(key.Get<HydrogenSecretBoxKey>(), data);
}

}  // namespace ae

#endif

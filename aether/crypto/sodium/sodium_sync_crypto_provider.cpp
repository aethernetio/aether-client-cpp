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

#include "aether/crypto/sodium/sodium_sync_crypto_provider.h"

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305

#  include <vector>
#  include <cassert>
#  include <utility>
#  include <algorithm>

#  include "aether/crypto/crypto_nonce.h"

namespace ae {

namespace _internal {
inline DataBuffer EncryptWithSymmetric(SodiumChachaKey const& secret_key,
                                       CryptoNonce const& nonce,
                                       DataBuffer const& raw_data) {
  std::vector<uint8_t> ciphertext(
      raw_data.size() + crypto_aead_chacha20poly1305_ABYTES + nonce.size());

  unsigned long long ciphertext_len;

  [[maybe_unused]] auto r = crypto_aead_chacha20poly1305_encrypt(
      ciphertext.data(), &ciphertext_len, raw_data.data(), raw_data.size(),
      nullptr, 0, nullptr, nonce.data(), secret_key.key.data());

  assert(r == 0);

  ciphertext.resize(static_cast<std::size_t>(ciphertext_len + nonce.size()));

  // add nonce to the end of ciphertext
  std::copy(
      std::begin(nonce), std::end(nonce),
      std::begin(ciphertext) + static_cast<std::ptrdiff_t>(ciphertext_len));

  return ciphertext;
}

inline DataBuffer DecryptWithSymmetric(SodiumChachaKey const& secret_key,
                                       DataBuffer const& encrypted_data) {
  assert(encrypted_data.size() > kNonceSize);

  auto nonce = CryptoNonce{};
  auto encrypted_data_size = encrypted_data.size() - nonce.size();

  // get nonce from the end of child_data_
  std::copy(
      encrypted_data.begin() + static_cast<std::ptrdiff_t>(encrypted_data_size),
      encrypted_data.end(), nonce.begin());

  auto decrypted_data = std::vector<uint8_t>(
      encrypted_data_size - crypto_aead_chacha20poly1305_ABYTES);
  unsigned long long decrypted_len;

  [[maybe_unused]] auto r = crypto_aead_chacha20poly1305_decrypt(
      decrypted_data.data(), &decrypted_len, nullptr, encrypted_data.data(),
      encrypted_data_size, nullptr, 0, nonce.data(), secret_key.key.data());

  assert(r == 0);

  return decrypted_data;
}
}  // namespace _internal

SodiumSyncEncryptProvider::SodiumSyncEncryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer SodiumSyncEncryptProvider::Encrypt(DataBuffer const& data) {
  auto key = key_provider_->GetKey();
  assert(key.Index() == CryptoKeyType::kSodiumChacha);

  return _internal::EncryptWithSymmetric(key.Get<SodiumChachaKey>(),
                                         key_provider_->Nonce(), data);
}

std::size_t SodiumSyncEncryptProvider::EncryptOverhead() const {
  return crypto_aead_chacha20poly1305_ABYTES + kNonceSize;
}

SodiumSyncDecryptProvider::SodiumSyncDecryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider)
    : key_provider_{std::move(key_provider)} {}

DataBuffer SodiumSyncDecryptProvider::Decrypt(DataBuffer const& data) {
  auto key = key_provider_->GetKey();
  assert(key.Index() == CryptoKeyType::kSodiumChacha);

  return _internal::DecryptWithSymmetric(key.Get<SodiumChachaKey>(), data);
}

}  // namespace ae

#endif

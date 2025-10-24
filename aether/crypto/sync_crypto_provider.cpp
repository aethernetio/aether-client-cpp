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

#include "aether/crypto/sync_crypto_provider.h"

#include <utility>

#include "aether/crypto/key.h"

#include "aether/crypto/sodium/sodium_sync_crypto_provider.h"
#include "aether/crypto/hydrogen/hydro_sync_crypto_provider.h"

namespace ae {

namespace _sync_internal {
template <typename KeyType>
std::unique_ptr<IEncryptProvider> CreateEncryptImpl(
    KeyType const&, std::unique_ptr<ISyncKeyProvider> /* key_provider */) {
  assert(false);
  return {};
}

template <typename KeyType>
std::unique_ptr<IDecryptProvider> CreateDecryptImpl(
    KeyType const&, std::unique_ptr<ISyncKeyProvider> /* key_provider */) {
  assert(false);
  return {};
}

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305

template <>
std::unique_ptr<IEncryptProvider> CreateEncryptImpl(
    SodiumChacha20Poly1305Key const&,
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  return make_unique<SodiumSyncEncryptProvider>(std::move(key_provider));
}

template <>
std::unique_ptr<IDecryptProvider> CreateDecryptImpl(
    SodiumChacha20Poly1305Key const&,
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  return make_unique<SodiumSyncDecryptProvider>(std::move(key_provider));
}
#endif

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
template <>
std::unique_ptr<IEncryptProvider> CreateEncryptImpl(
    HydrogenSecretBoxKey const&,
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  return make_unique<HydroSyncEncryptProvider>(std::move(key_provider));
}

template <>
std::unique_ptr<IDecryptProvider> CreateDecryptImpl(
    HydrogenSecretBoxKey const&,
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  return make_unique<HydroSyncDecryptProvider>(std::move(key_provider));
}
#endif

}  // namespace _sync_internal

SyncEncryptProvider::SyncEncryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  auto key = key_provider->GetKey();
  impl_ = std::visit(
      [&](auto key_type) {
        return _sync_internal::CreateEncryptImpl(key_type,
                                                 std::move(key_provider));
      },
      key);
  assert(impl_);
}

DataBuffer SyncEncryptProvider::Encrypt(DataBuffer const& data) {
  return impl_->Encrypt(data);
}

std::size_t SyncEncryptProvider::EncryptOverhead() const {
  return impl_->EncryptOverhead();
}

SyncDecryptProvider::SyncDecryptProvider(
    std::unique_ptr<ISyncKeyProvider> key_provider) {
  auto key = key_provider->GetKey();
  impl_ = std::visit(
      [&](auto key_type) {
        return _sync_internal::CreateDecryptImpl(key_type,
                                                 std::move(key_provider));
      },
      key);
  assert(impl_);
}

DataBuffer SyncDecryptProvider::Decrypt(DataBuffer const& data) {
  return impl_->Decrypt(data);
}

}  // namespace ae

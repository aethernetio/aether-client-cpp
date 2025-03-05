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

#ifndef AETHER_CRYPTO_HYDROGEN_HYDRO_SYNC_CRYPTO_PROVIDER_H_
#define AETHER_CRYPTO_HYDROGEN_HYDRO_SYNC_CRYPTO_PROVIDER_H_

#include "aether/config.h"

#if AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK

#  include "aether/memory.h"

#  include "aether/crypto/icrypto_provider.h"
#  include "aether/crypto/ikey_provider.h"

namespace ae {
class HydroSyncEncryptProvider : public IEncryptProvider {
 public:
  explicit HydroSyncEncryptProvider(
      std::unique_ptr<ISyncKeyProvider> key_provider);

  DataBuffer Encrypt(DataBuffer const& data) override;
  std::size_t EncryptOverhead() const override;

 private:
  std::unique_ptr<ISyncKeyProvider> key_provider_;
};

class HydroSyncDecryptProvider : public IDecryptProvider {
 public:
  explicit HydroSyncDecryptProvider(
      std::unique_ptr<ISyncKeyProvider> key_provider);

  DataBuffer Decrypt(DataBuffer const& data) override;

 private:
  std::unique_ptr<ISyncKeyProvider> key_provider_;
};
}  // namespace ae

#endif
#endif  // AETHER_CRYPTO_HYDROGEN_HYDRO_SYNC_CRYPTO_PROVIDER_H_

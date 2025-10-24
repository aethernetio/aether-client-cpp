/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_REGISTRATION_REGISTRATION_CRYPTO_PROVIDER_H_
#define AETHER_REGISTRATION_REGISTRATION_CRYPTO_PROVIDER_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/crypto/icrypto_provider.h"
#  include "aether/crypto/sync_crypto_provider.h"
#  include "aether/crypto/async_crypto_provider.h"

namespace ae {
class RegRequestEncryption final : public IEncryptProvider {
 public:
  RegRequestEncryption();

  // set key for encryption
  void SetKey(Key&& key);

  DataBuffer Encrypt(DataBuffer const& data) override;
  std::size_t EncryptOverhead() const override;

 private:
  std::unique_ptr<AsyncEncryptProvider> encrypt_provider_;
};

class RegResponseDecryption final : public IDecryptProvider {
 public:
  RegResponseDecryption();

  // get new key
  Key ReturnKey();

  DataBuffer Decrypt(DataBuffer const& data) override;

 private:
  std::unique_ptr<SyncDecryptProvider> decrypt_provider_;
};

class RegistrationCryptoProvider final : public ICryptoProvider {
 public:
  RegistrationCryptoProvider();

  void SetEncryptionKey(Key key);
  Key GetDecryptionKey();

  IEncryptProvider* encryptor() override { return &encrypt_provider_; }
  IDecryptProvider* decryptor() override { return &decrypt_provider_; }

 private:
  RegRequestEncryption encrypt_provider_;
  RegResponseDecryption decrypt_provider_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_REGISTRATION_CRYPTO_PROVIDER_H_

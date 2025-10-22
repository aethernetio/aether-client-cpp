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

#include "aether/registration/registration_crypto_provider.h"

#if AE_SUPPORT_REGISTRATION

#  include <utility>

#  include "aether/crypto/key_gen.h"
#  include "aether/crypto/ikey_provider.h"

namespace ae {
class RegAsyncKeyProvider final : public IAsyncKeyProvider {
 public:
  explicit RegAsyncKeyProvider(Key public_key)
      : public_key_{std::move(public_key)} {}

  Key PublicKey() const override { return public_key_; }
  Key SecretKey() const override { return {}; }

 private:
  Key public_key_;
};

class RegSyncKeyProvider final : public ISyncKeyProvider {
 public:
  RegSyncKeyProvider() : nonce_{} {
    nonce_.Init();
    [[maybe_unused]] auto res = CryptoSyncKeygen(key_);
    assert(res);
  }

  Key GetKey() const override { return key_; }

  CryptoNonce const& Nonce() const override {
    nonce_.Next();
    return nonce_;
  }

 private:
  Key key_;
  mutable CryptoNonce nonce_;
};

RegRequestEncryption::RegRequestEncryption() = default;

void RegRequestEncryption::SetKey(Key&& key) {
  encrypt_provider_ = std::make_unique<AsyncEncryptProvider>(
      std::make_unique<RegAsyncKeyProvider>(std::move(key)));
}

DataBuffer RegRequestEncryption::Encrypt(DataBuffer const& data) {
  assert(encrypt_provider_);
  return encrypt_provider_->Encrypt(data);
}

std::size_t RegRequestEncryption::EncryptOverhead() const {
  assert(encrypt_provider_);
  return encrypt_provider_->EncryptOverhead();
}

RegResponseDecryption::RegResponseDecryption() = default;

Key RegResponseDecryption::ReturnKey() {
  auto key_provider = std::make_unique<RegSyncKeyProvider>();
  auto* provider = key_provider.get();
  decrypt_provider_ =
      std::make_unique<SyncDecryptProvider>(std::move(key_provider));
  return provider->GetKey();
}

DataBuffer RegResponseDecryption::Decrypt(DataBuffer const& data) {
  assert(decrypt_provider_);
  return decrypt_provider_->Decrypt(data);
}

RegistrationCryptoProvider::RegistrationCryptoProvider() = default;

void RegistrationCryptoProvider::SetEncryptionKey(Key key) {
  encrypt_provider_.SetKey(std::move(key));
}

Key RegistrationCryptoProvider::GetDecryptionKey() {
  return decrypt_provider_.ReturnKey();
}

}  // namespace ae

#endif

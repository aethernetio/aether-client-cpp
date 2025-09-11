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

#ifndef AETHER_REGISTRATION_REGISTRATION_KEY_PROVIDER_H_
#define AETHER_REGISTRATION_REGISTRATION_KEY_PROVIDER_H_

#include "aether/crypto/ikey_provider.h"

namespace ae {
class RegistrationAsyncKeyProvider : public IAsyncKeyProvider {
 public:
  RegistrationAsyncKeyProvider();

  Key PublicKey() const override;
  Key SecretKey() const override;

  void set_public_key(Key key);
  void set_secret_key(Key key);

 private:
  Key public_key_;
  Key secret_key_;
};

class RegistrationSyncKeyProvider : public ISyncKeyProvider {
 public:
  RegistrationSyncKeyProvider();

  Key GetKey() const override;
  CryptoNonce const& Nonce() const override;

  void set_key(Key key);

 private:
  mutable CryptoNonce nonce_;
  Key sync_key_;
};
}  // namespace ae

#endif  // AETHER_REGISTRATION_REGISTRATION_KEY_PROVIDER_H_

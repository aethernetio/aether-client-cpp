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

#include "aether/ae_actions/registration/registration_key_provider.h"

namespace ae {
RegistrationAsyncKeyProvider::RegistrationAsyncKeyProvider() = default;

Key RegistrationAsyncKeyProvider::PublicKey() const { return public_key_; }

Key RegistrationAsyncKeyProvider::SecretKey() const { return secret_key_; }

void RegistrationAsyncKeyProvider::set_public_key(Key key) {
  public_key_ = key;
}

void RegistrationAsyncKeyProvider::set_secret_key(Key key) {
  secret_key_ = key;
}

RegistrationSyncKeyProvider::RegistrationSyncKeyProvider() : nonce_{} {
  nonce_.Init();
}

Key RegistrationSyncKeyProvider::GetKey() const { return sync_key_; }

CryptoNonce const& RegistrationSyncKeyProvider::Nonce() const {
  nonce_.Next();
  return nonce_;
}

void RegistrationSyncKeyProvider::set_key(Key key) { sync_key_ = key; }

}  // namespace ae

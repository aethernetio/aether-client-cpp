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

#include "aether/server_keys.h"

#include "aether/crypto/key_gen.h"

namespace ae {
ServerKeys::ServerKeys(ServerId server_id, const Key& master_key)
    : server_id_{server_id}, master_key_{master_key} {
  nonce_.Init();
  nonce_.Next();
  Derive(server_id, master_key, key_number_);
}

CryptoNonce const& ServerKeys::nonce() const { return nonce_; }

Key const& ServerKeys::client_to_server() const {
  return client_to_server_key_;
}

Key const& ServerKeys::server_to_client() const {
  return server_to_client_key_;
}

void ServerKeys::Next() { nonce_.Next(); }

void ServerKeys::Derive(ServerId server_id, const Key& master_key,
                        std::uint32_t key_number) {
  [[maybe_unused]] auto res =
      CryptoSyncKeyDerive(master_key, server_id, key_number,
                          client_to_server_key_, server_to_client_key_);
  assert(res);
}
}  // namespace ae

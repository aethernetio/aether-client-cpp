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

#ifndef AETHER_SERVER_KEYS_H_
#define AETHER_SERVER_KEYS_H_

#include <cstdint>
#include <cassert>

#include "third_party/aethernet-numeric/numeric/tiered_int.h"

#include "aether/crypto/key.h"
#include "aether/types/server_id.h"
#include "aether/crypto/crypto_nonce.h"

namespace ae {
class ServerKeys {
 public:
  ServerKeys() = default;
  ServerKeys(ServerId server_id, const Key& master_key);

  CryptoNonce const& nonce() const;
  Key const& client_to_server() const;
  Key const& server_to_client() const;

  void Next();

  AE_REFLECT_MEMBERS(server_id_, master_key_, key_number_, nonce_,
                     client_to_server_key_, server_to_client_key_)

 private:
  void Derive(ServerId server_id, const Key& master_key,
              std::uint32_t key_number);

  ServerId server_id_{};
  Key master_key_;
  TieredInt<std::uint32_t, std::uint8_t, 250> key_number_{};
  CryptoNonce nonce_;
  Key client_to_server_key_;
  Key server_to_client_key_;
};

};  // namespace ae

#endif  // AETHER_SERVER_KEYS_H_

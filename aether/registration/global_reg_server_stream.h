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

#ifndef AETHER_REGISTRATION_GLOBAL_REG_SERVER_STREAM_H_
#define AETHER_REGISTRATION_GLOBAL_REG_SERVER_STREAM_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/stream_api/istream.h"

#  include "aether/stream_api/crypto_gate.h"
#  include "aether/methods/server_reg_api/server_registration_api.h"
#  include "aether/methods/client_reg_api/client_global_reg_api.h"

namespace ae {
class GlobalRegServerStream final : public ByteStream {
 public:
  GlobalRegServerStream(ProtocolContext& protocol_context,
                        ClientGlobalRegApi& client_global_api,
                        ServerRegistrationApi& server_reg_api, std::string salt,
                        std::string password_suffix,
                        std::vector<uint32_t> passwords, Uid parent_uid,
                        Key return_key,
                        std::unique_ptr<IEncryptProvider> crypto_encrypt,
                        std::unique_ptr<IDecryptProvider> crypto_decrypt);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  void LinkOut(OutStream& out) override;

 private:
  void ReadData(DataBuffer const& data);

  ProtocolContext* protocol_context_;
  ClientGlobalRegApi* client_global_api_;
  ServerRegistrationApi* server_reg_api_;
  std::string salt_;
  std::string password_suffix_;
  std::vector<uint32_t> passwords_;
  Uid parent_uid_;
  Key return_key_;
  CryptoGate crypto_gate_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_GLOBAL_REG_SERVER_STREAM_H_

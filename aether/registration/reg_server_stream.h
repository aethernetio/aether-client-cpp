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

#ifndef AETHER_REGISTRATION_REG_SERVER_STREAM_H_
#define AETHER_REGISTRATION_REG_SERVER_STREAM_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/stream_api/istream.h"

#  include "aether/stream_api/crypto_gate.h"
#  include "aether/api_protocol/protocol_context.h"
#  include "aether/methods/server_reg_api/root_api.h"
#  include "aether/methods/client_reg_api/client_reg_api.h"
#  include "aether/methods/client_reg_api/client_reg_root_api.h"

namespace ae {
class RegServerStream final : public ByteStream {
 public:
  RegServerStream(ActionContext action_context,
                  ProtocolContext& protocol_context,
                  ClientApiRegSafe& client_api,
                  CryptoLibProfile crypto_lib_profile,
                  std::unique_ptr<IEncryptProvider> crypto_encrypt,
                  std::unique_ptr<IDecryptProvider> crypto_decrypt);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  void LinkOut(OutStream& out) override;

 private:
  void ReadRoot(DataBuffer const& data);
  void ReadRootEnter(DataBuffer const& data);
  void GlobalApiEnter(DataBuffer const& data);

  ProtocolContext* protocol_context_;
  ClientApiRegSafe* client_api_;
  CryptoLibProfile crypto_lib_profile_;
  CryptoGate crypto_gate_;
  RootApi root_api_;
  ClientRegRootApi client_reg_root_api_;
  Subscription root_enter_sub_;
  Subscription client_safe_enter_sub_;
};
}  // namespace ae

#endif
#endif  // AETHER_REGISTRATION_REG_SERVER_STREAM_H_

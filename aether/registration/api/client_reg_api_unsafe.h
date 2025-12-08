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

#ifndef AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_
#define AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_

#include "aether/config.h"
#if AE_SUPPORT_REGISTRATION

#  include "aether/crypto/icrypto_provider.h"
#  include "aether/api_protocol/api_protocol.h"

#  include "aether/registration/api/client_reg_api_safe.h"
#  include "aether/registration/api/global_reg_client_api.h"

namespace ae {
class ClientRegRootApi final : public ApiClassImpl<ClientRegRootApi> {
 public:
  explicit ClientRegRootApi(ProtocolContext& protocol_context,
                            IDecryptProvider& root_decrypt_provider,
                            IDecryptProvider& global_decrypt_provider);

  void Enter(SubApiImpl<ClientRegApiSafe> sub_api);
  void EnterGlobal(SubApiImpl<GlobalRegClientApi> sub_api);

  ReturnResultApi return_result;

  AE_METHODS(RegMethod<03, &ClientRegRootApi::Enter>,
             RegMethod<04, &ClientRegRootApi::EnterGlobal>,
             ExtApi<&ClientRegRootApi::return_result>);

 private:
  IDecryptProvider* root_decrypt_provider_;
  IDecryptProvider* global_decrypt_provider_;

  ClientRegApiSafe client_reg_api_;
  GlobalRegClientApi global_reg_client_api_;
};
}  // namespace ae
#endif

#endif  // AETHER_REGISTRATION_API_CLIENT_REG_API_UNSAFE_H_

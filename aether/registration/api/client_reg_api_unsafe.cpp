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

#include "aether/registration/api/client_reg_api_unsafe.h"

#if AE_SUPPORT_REGISTRATION
namespace ae {
ClientRegRootApi::ClientRegRootApi(ProtocolContext& protocol_context,
                                   IDecryptProvider& root_decrypt_provider,
                                   IDecryptProvider& global_decrypt_provider)
    : ApiClassImpl{protocol_context},
      return_result{protocol_context},
      root_decrypt_provider_{&root_decrypt_provider},
      global_decrypt_provider_{&global_decrypt_provider},
      client_reg_api_{protocol_context},
      global_reg_client_api_{protocol_context} {}

void ClientRegRootApi::Enter(SubApiImpl<ClientRegApiSafe> sub_api) {
  sub_api.Parse(client_reg_api_, [this](auto const& data) {
    return root_decrypt_provider_->Decrypt(data);
  });
}

void ClientRegRootApi::EnterGlobal(SubApiImpl<GlobalRegClientApi> sub_api) {
  sub_api.Parse(global_reg_client_api_, [this](auto const& data) {
    return global_decrypt_provider_->Decrypt(data);
  });
}

}  // namespace ae
#endif

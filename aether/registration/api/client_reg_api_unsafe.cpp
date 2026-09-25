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
ClientRegRootApi::ClientRegRootApi(IDecryptProvider& root_decrypt_provider,
                                   IDecryptProvider& global_decrypt_provider)
    : root_decrypt_provider_{&root_decrypt_provider},
      global_decrypt_provider_{&global_decrypt_provider} {}

void ClientRegRootApi::Enter(SubApi<ClientRegApiSafe> sub_api) {
  auto unsafe_data =
      root_decrypt_provider_->Decrypt(std::move(sub_api).buffer());
  auto parser = ApiParser{server_context().protocol_context(), unsafe_data};
  parser.Parse(client_reg_api_);
}

void ClientRegRootApi::EnterGlobal(SubApi<GlobalRegClientApi> sub_api) {
  auto unsafe_data =
      global_decrypt_provider_->Decrypt(std::move(sub_api).buffer());
  auto parser = ApiParser{server_context().protocol_context(), unsafe_data};
  parser.Parse(global_reg_client_api_);
}

}  // namespace ae
#endif

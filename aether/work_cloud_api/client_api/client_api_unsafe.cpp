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

#include "aether/work_cloud_api/client_api/client_api_unsafe.h"

#include "aether/tele/tele.h"

namespace ae {
ClientApiUnsafe::ClientApiUnsafe(ProtocolContext& protocol_context,
                                 IDecryptProvider& decrypt_provider)
    : ApiClassImpl{protocol_context},
      return_result{protocol_context},
      decrypt_provider_{&decrypt_provider},
      client_safe_api_{protocol_context} {}

void ClientApiUnsafe::SendSafeApiData(ApiParser& /*parser*/,
                                      SubApiImpl<ClientApiSafe> sub_api) {
  sub_api.Parse(client_safe_api_, [this](auto const& data) {
    auto decrypted = Decrypt(data);
    AE_TELED_DEBUG("Client api unsafe data {}", decrypted);
    return decrypted;
  });
}

DataBuffer ClientApiUnsafe::Decrypt(DataBuffer const& data) {
  return decrypt_provider_->Decrypt(data);
}

}  // namespace ae

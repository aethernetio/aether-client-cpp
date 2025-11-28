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

#ifndef AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_
#define AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_

#include "aether/types/data_buffer.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/api_protocol/api_protocol.h"

#include "aether/work_cloud_api/client_api/client_api_safe.h"

namespace ae {
class ClientApiUnsafe : public ApiClassImpl<ClientApiUnsafe> {
 public:
  explicit ClientApiUnsafe(ProtocolContext& protocol_context,
                           IDecryptProvider& decrypt_provider);

  void SendSafeApiData(SubApiImpl<ClientApiSafe> sub_api);

  ReturnResultApi return_result;

  AE_METHODS(RegMethod<4, &ClientApiUnsafe::SendSafeApiData>,
             ExtApi<&ClientApiUnsafe::return_result>);

  ClientApiSafe& client_api_safe() { return client_safe_api_; }

 private:
  DataBuffer Decrypt(DataBuffer const& data);

  IDecryptProvider* decrypt_provider_;
  ClientApiSafe client_safe_api_;
};
}  // namespace ae
#endif  // AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_UNSAFE_H_

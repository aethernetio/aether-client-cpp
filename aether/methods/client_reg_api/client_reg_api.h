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

#ifndef AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_API_H_
#define AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include <string>
#  include <cstdint>
#  include <vector>

#  include "aether/crc.h"
#  include "aether/crypto/signed_key.h"

#  include "aether/api_protocol/api_protocol.h"
#  include "aether/stream_api/stream_api.h"
#  include "aether/methods/server_descriptor.h"

namespace ae {
struct PowParams {
  AE_REFLECT_MEMBERS(salt, password_suffix, pool_size, max_hash_value,
                     global_key)
  std::string salt;
  std::string password_suffix;
  std::uint8_t pool_size;
  std::uint32_t max_hash_value;
  SignedKey global_key;
};

class ClientApiRegSafe : public ApiClass,
                         ExtendsApi<ReturnResultApi, StreamApi> {
 public:
  struct GetKeysResponse {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientApiRegSafe::GetKeysResponse");

    RequestId req_id;
    SignedKey signed_key;
  };

  struct ResponseWorkProofData {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientApiRegSafe::ResponseWorkProofData");

    RequestId req_id;
    PowParams pow_params;
  };

  struct ResolveServersResponse {
    static constexpr auto kMessageId = crc32::checksum_from_literal(
        "ClientApiRegSafe::ResolveServersResponse");

    RequestId req_id;
    std::vector<ServerDescriptor> servers;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override;
};
}  // namespace ae

#endif
#endif  // AETHER_METHODS_CLIENT_REG_API_CLIENT_REG_API_H_

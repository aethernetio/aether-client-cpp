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

#ifndef AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_
#define AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <string>
#  include <vector>

#  include "aether/api_protocol/send_result.h"
#  include "aether/common.h"
#  include "aether/crypto/key.h"
#  include "aether/uid.h"
#  include "aether/crypto/crypto_definitions.h"

#  include "aether/stream_api/stream_api.h"
#  include "aether/api_protocol/child_data.h"
#  include "aether/api_protocol/api_protocol.h"

namespace ae {
class ServerRegistrationApi : public ApiClass {
 public:
  // start a new stream from GlobalRegClientApi to GlobalRegServerApi with
  // CryptoStream
  struct Registration : public Message<Registration> {
    static constexpr MessageId kMessageCode = 30;

    AE_REFLECT_MEMBERS(stream_id, salt, password_suffix, passwords, parent_uid_,
                       return_key)

    StreamId stream_id;
    std::string salt;
    std::string password_suffix;
    std::vector<uint32_t> passwords;
    Uid parent_uid_;
    Key return_key;
  };

  struct RequestProofOfWorkData : public Message<RequestProofOfWorkData> {
    static constexpr MessageId kMessageCode = 40;

    AE_REFLECT_MEMBERS(request_id, parent_id, pow_method, return_key)

    RequestId request_id;
    Uid parent_id;
    PowMethod pow_method;
    Key return_key;
  };

  struct ResolveServers : public Message<ResolveServers> {
    static constexpr MessageId kMessageCode = 70;
    AE_REFLECT_MEMBERS(request_id, servers)

    RequestId request_id;
    std::vector<ServerId> servers;
  };

  void Pack(Registration&& message, ApiPacker& packer);
  void Pack(RequestProofOfWorkData&& message, ApiPacker& packer);
  void Pack(ResolveServers&& message, ApiPacker& packer);
};
}  // namespace ae

#endif
#endif  // AETHER_METHODS_SERVER_REG_API_SERVER_REGISTRATION_API_H_

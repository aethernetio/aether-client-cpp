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

#ifndef AETHER_METHODS_CLIENT_REG_API_CLIENT_GLOBAL_REG_API_H_
#define AETHER_METHODS_CLIENT_REG_API_CLIENT_GLOBAL_REG_API_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION
#  include <vector>

#  include "aether/crc.h"
#  include "aether/uid.h"
#  include "aether/common.h"
#  include "aether/api_protocol/api_protocol.h"
#  include "aether/stream_api/stream_api.h"

namespace ae {

struct RegistrationResponse {
  AE_REFLECT_MEMBERS(ephemeral_uid, uid, cloud)
  Uid ephemeral_uid;
  Uid uid;
  std::vector<ServerId> cloud;
};

class ClientGlobalRegApi : public ApiClass,
                           ExtendsApi<ReturnResultApi, StreamApi> {
 public:
  struct ConfirmRegistration {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientGlobalRegApi::ConfirmRegistration");

    RequestId req_id;
    RegistrationResponse registration_response;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override;
};
}  // namespace ae
#endif
#endif  // AETHER_METHODS_CLIENT_REG_API_CLIENT_GLOBAL_REG_API_H_

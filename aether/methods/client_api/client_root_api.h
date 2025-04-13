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

#ifndef AETHER_METHODS_CLIENT_API_CLIENT_ROOT_API_H_
#define AETHER_METHODS_CLIENT_API_CLIENT_ROOT_API_H_

#include "aether/uid.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/stream_api/stream_api.h"

namespace ae {
class ClientRootApi : public ApiClass,
                      public ExtendsApi<ReturnResultApi, StreamApi> {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("ClientRootApi");

  struct SendSafeApiData : public Message<SendSafeApiData> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientRootApi::SendSafeApiData");
    static constexpr auto kMessageCode = 6;

    AE_REFLECT_MEMBERS(uid, data)

    Uid uid;
    DataBuffer data;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override;

  void Execute(SendSafeApiData&& message, ApiParser& api_parser);
};
}  // namespace ae
#endif  // AETHER_METHODS_CLIENT_API_CLIENT_ROOT_API_H_

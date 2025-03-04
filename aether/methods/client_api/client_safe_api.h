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

#ifndef AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_
#define AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_

#include "aether/crc.h"
#include "aether/transport/data_buffer.h"
#include "aether/uid.h"

#include "aether/api_protocol/api_protocol.h"
#include "aether/stream_api/stream_api.h"

namespace ae {

class ClientSafeApi : public ApiClass,
                      public ExtendsApi<ReturnResultApi, StreamApi> {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("ClientSafeApi");

  // start a new client level stream
  struct StreamToClient : public Message<StreamToClient> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientSafeApi::StreamToClient");
    static constexpr auto kMessageCode = 3;

    AE_REFLECT_MEMBERS(uid, stream_id)

    Uid uid;
    StreamId stream_id;
  };

  struct SendMessage : public Message<SendMessage> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ClientSafeApi::SendMessage");
    static constexpr auto kMessageCode = 4;

    AE_REFLECT_MEMBERS(uid, data)

    Uid uid;
    DataBuffer data;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override;

  void Execute(StreamToClient&& message, ApiParser& api_parser);
  void Execute(SendMessage&& message, ApiParser& api_parser);
};
}  // namespace ae

#endif  // AETHER_METHODS_CLIENT_API_CLIENT_SAFE_API_H_

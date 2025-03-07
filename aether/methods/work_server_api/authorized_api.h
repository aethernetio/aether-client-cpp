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

#ifndef AETHER_METHODS_WORK_SERVER_API_AUTHORIZED_API_H_
#define AETHER_METHODS_WORK_SERVER_API_AUTHORIZED_API_H_

#include "aether/crc.h"
#include "aether/uid.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/transport/data_buffer.h"
#include "aether/stream_api/stream_api.h"

namespace ae {

class AuthorizedApi : public ApiClass {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("AuthorizedApi");

  // Just ping the server to finalize authorization and stream
  struct Ping : public Message<Ping> {
    static constexpr auto kMessageCode = 6;
    AE_REFLECT()
  };

  struct OpenStreamToClient : public Message<OpenStreamToClient> {
    static constexpr auto kMessageCode = 9;

    AE_REFLECT_MEMBERS(uid, stream_id)

    Uid uid;
    StreamId stream_id;
  };

  struct SendMessage : public Message<SendMessage> {
    static constexpr auto kMessageCode = 10;

    AE_REFLECT_MEMBERS(request_id, uid, data)

    RequestId request_id;
    Uid uid;
    DataBuffer data;
  };

  struct Resolvers : public Message<Resolvers> {
    static constexpr auto kMessageCode = 12;

    AE_REFLECT_MEMBERS(servers_stream_id, cloud_stream_id)

    StreamId servers_stream_id;
    StreamId cloud_stream_id;
  };

  void Pack(Ping&& message, ApiPacker& api_packer);
  void Pack(OpenStreamToClient&& message, ApiPacker& api_packer);
  void Pack(SendMessage&& message, ApiPacker& api_packer);
  void Pack(Resolvers&& message, ApiPacker& api_packer);
};
}  // namespace ae

#endif  // AETHER_METHODS_WORK_SERVER_API_AUTHORIZED_API_H_

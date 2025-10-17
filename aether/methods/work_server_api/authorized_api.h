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

#include "aether/types/uid.h"
#include "aether/methods/telemetric.h"
#include "aether/types/data_buffer.h"
#include "aether/stream_api/stream_api.h"
#include "aether/api_protocol/api_method.h"

namespace ae {

class AuthorizedApi {
 public:
  AuthorizedApi(ProtocolContext& protocol_context,
                ActionContext action_context);

  Method<06, ApiPromisePtr<void>(std::uint64_t next_ping_duration)> ping;
  Method<10, void(Uid uid, DataBuffer data)> send_message;
  Method<12, void(StreamId servers_stream_id, StreamId cloud_stream_id)>
      resolvers;
  Method<16, ApiPromisePtr<void>(Uid uid)> check_access_for_send_message;

  Method<70, void(Telemetric telemetric)> send_telemetric;
};
}  // namespace ae

#endif  // AETHER_METHODS_WORK_SERVER_API_AUTHORIZED_API_H_

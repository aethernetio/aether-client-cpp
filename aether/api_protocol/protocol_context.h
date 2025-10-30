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

#ifndef AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_
#define AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_

#include <map>
#include <stack>
#include <cstdint>
#include <functional>

#include "aether/api_protocol/request_id.h"

namespace ae {
class ApiParser;

class ProtocolContext {
 public:
  ProtocolContext();
  ~ProtocolContext();

  void AddSendResultCallback(RequestId request_id,
                             std::function<void(ApiParser& parser)> callback);
  void AddSendErrorCallback(
      RequestId request_id,
      std::function<void(std::uint8_t error_type, std::uint32_t error_code)>
          callback);

  void SetSendResultResponse(RequestId request_id, ApiParser& parser);
  void SetSendErrorResponse(RequestId req_id, std::uint8_t error_type,
                            std::uint32_t error_code, ApiParser& parser);

  void PushPacketStack(class PacketStack& packet_stack);
  void PopPacketStack();
  class PacketStack* packet_stack();

 private:
  std::map<RequestId, std::function<void(ApiParser& parser)>>
      send_result_events_;
  std::map<RequestId, std::function<void(std::uint8_t error_type,
                                         std::uint32_t error_code)>>
      send_error_events_;

  std::stack<class PacketStack*> packet_stacks_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_

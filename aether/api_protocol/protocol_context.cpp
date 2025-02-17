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

#include "aether/api_protocol/protocol_context.h"

#include <cassert>

#include "aether/api_protocol/api_protocol.h"

#include "aether/tele/tele.h"

namespace ae {
ProtocolContext::ProtocolContext() = default;
ProtocolContext::~ProtocolContext() = default;

void ProtocolContext::PushApiClass(std::uint32_t class_id,
                                   void const* api_class) {
  api_class_map_[class_id] = api_class;
}

void ProtocolContext::PopApiClass(std::uint32_t class_id) {
  api_class_map_.erase(class_id);
}

void ProtocolContext::PushUserData(void* data) { user_data_stack_.push(data); }
void ProtocolContext::PopUserData() {
  assert(!user_data_stack_.empty());
  user_data_stack_.pop();
}

void* ProtocolContext::TopUserData() {
  if (user_data_stack_.empty()) {
    return nullptr;
  }
  return user_data_stack_.top();
}

void ProtocolContext::AddSendResultCallback(
    RequestId request_id, std::function<void(ApiParser& parser)> callback) {
  send_result_events_.emplace(request_id, std::move(callback));
}

void ProtocolContext::SetSendResultResponse(RequestId request_id,
                                            ApiParser& parser) {
  auto it = send_result_events_.find(request_id);
  if (it != send_result_events_.end()) {
    it->second(parser);
    send_result_events_.erase(it);
  } else {
    AE_TELED_DEBUG("No callback for request id {} cancel parse", request_id);
    parser.Cancel();
  }
}

}  // namespace ae

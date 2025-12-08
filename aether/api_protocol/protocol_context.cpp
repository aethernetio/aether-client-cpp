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

void ProtocolContext::AddSendResultCallback(RequestId request_id,
                                            SendResultCb callback) {
  send_result_events_.emplace(request_id, std::move(callback));
}

void ProtocolContext::AddSendErrorCallback(RequestId request_id,
                                           SendErrorCb callback) {
  send_error_events_.emplace(request_id, std::move(callback));
}

void ProtocolContext::SetSendResultResponse(RequestId request_id) {
  auto it = send_result_events_.find(request_id);
  if (it != send_result_events_.end()) {
    it->second();
    send_result_events_.erase(it);
  } else {
    AE_TELED_DEBUG("No callback for request id {} cancel parse", request_id);
    parser()->Cancel();
  }

  send_error_events_.erase(request_id);
}

void ProtocolContext::SetSendErrorResponse(RequestId req_id,
                                           std::uint8_t error_type,
                                           std::uint32_t error_code) {
  auto it = send_error_events_.find(req_id);
  if (it != std::end(send_error_events_)) {
    it->second(error_type, error_code);
    send_error_events_.erase(it);
  } else {
    AE_TELED_DEBUG("No callback for error with request id {}", req_id);
    std::cerr << "SendError: id " << req_id
              << " type: " << static_cast<int>(error_type)
              << " error code: " << error_code << std::endl;
    parser()->Cancel();
  }
  send_result_events_.erase(req_id);
}

void ProtocolContext::PushPacketStack(class PacketStack& packet_stack) {
  packet_stacks_.push(&packet_stack);
}

void ProtocolContext::PopPacketStack() { packet_stacks_.pop(); }

class PacketStack* ProtocolContext::packet_stack() {
  if (packet_stacks_.empty()) {
    return nullptr;
  }
  return packet_stacks_.top();
}

void ProtocolContext::PushParser(ApiParser& parser) { parsers_.push(&parser); }

void ProtocolContext::PopParser() { parsers_.pop(); }

ApiParser* ProtocolContext::parser() {
  if (parsers_.empty()) {
    return nullptr;
  }
  return parsers_.top();
}

void ProtocolContext::PushPacker(ApiPacker& packer) { packers_.push(&packer); }

void ProtocolContext::PopPacker() { packers_.pop(); }

ApiPacker* ProtocolContext::packer() {
  if (packers_.empty()) {
    return nullptr;
  }
  return packers_.top();
}

}  // namespace ae

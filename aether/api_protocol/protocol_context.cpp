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
#include <cstddef>
#include <cstdint>

#include "aether/api_protocol/api_protocol.h"

#include "aether/tele/tele.h"

namespace ae {
ProtocolContext::ProtocolContext() = default;
ProtocolContext::~ProtocolContext() {
  while (!pending_responses_.empty()) {
    auto entry = TakeOldestPending();
    DestroyPending(entry);
  }
}

void ProtocolContext::SetSendResultResponse(RequestId request_id) {
  auto entry = TakePending(request_id);
  if (entry.response == nullptr) {
    AE_TELED_DEBUG("No callback for request id {} cancel parse", request_id);
    parser()->Cancel();
  }

  auto* p = parser();
  assert(p != nullptr && "Parser shouldn't be null");
  entry.response->OnResult(*p);
  DestroyPending(entry);
}

void ProtocolContext::SetSendErrorResponse(RequestId req_id,
                                           std::uint8_t error_type,
                                           std::uint32_t error_code) {
  auto entry = TakePending(req_id);
  if (entry.response == nullptr) {
    AE_TELED_DEBUG("No callback for error with request id {}", req_id);
    parser()->Cancel();
  }

  entry.response->OnError(error_type, static_cast<std::int32_t>(error_code));
  DestroyPending(entry);
}

void ProtocolContext::EvictPending(PendingEntry const& entry) {
  assert(entry.response != nullptr &&
         "EvictPending requires a pending response entry");
  entry.response->OnEvicted();
  DestroyPending(entry);
}

void ProtocolContext::DestroyPending(PendingEntry const& entry) {
  assert(entry.response != nullptr &&
         "DestroyPending requires a pending response entry");
  pending_response_pool_.destroy(entry.response);
}

void ProtocolContext::PreparePendingResponseSlot(RequestId request_id) {
  // ensure there is one pending response for request_id
  auto existing_entry = TakePending(request_id);
  if (existing_entry.response != nullptr) {
    EvictPending(existing_entry);
    return;
  }

  // ensure there is enough in pool for new pending response
  // oldest pending should be evicted
  if (pending_responses_.full()) {
    auto oldest_entry = TakeOldestPending();
    EvictPending(oldest_entry);
  }

  assert(!pending_responses_.full() &&
         "Pending response registry must have a free slot");
  assert(pending_response_pool_.available() != 0U &&
         "Pending response pool must have a free slot");
}

ProtocolContext::PendingEntry ProtocolContext::TakePending(
    RequestId request_id) {
  auto* it =
      std::find_if(std::begin(pending_responses_), std::end(pending_responses_),
                   [request_id](auto const& pe) noexcept {
                     return pe.request_id == request_id;
                   });
  if (it == std::end(pending_responses_)) {
    return PendingEntry{RequestId{}, nullptr};
  }

  auto found = *it;
  pending_responses_.erase(it);
  return found;
}

ProtocolContext::PendingEntry ProtocolContext::TakeOldestPending() {
  assert(!pending_responses_.empty() &&
         "TakeOldestPending requires a pending response");

  // oldest is the first
  auto* it = pending_responses_.begin();
  auto found = *it;
  pending_responses_.erase(it);

  return found;
}

void ProtocolContext::PushPacketStack(PacketStack& packet_stack) {
  assert(!packet_stacks_.full() &&
         "AE_API_PROTOCOL_MAX_PACKET_STACK_DEPTH exceeded");
  packet_stacks_.push(&packet_stack);
}

void ProtocolContext::PopPacketStack() {
  assert(!packet_stacks_.empty() && "Packet stack context is empty");
  packet_stacks_.pop();
}

PacketStack* ProtocolContext::packet_stack() {
  if (packet_stacks_.empty()) {
    return nullptr;
  }
  auto* packet_stack = packet_stacks_.top();
  assert(packet_stack != nullptr && "Packet stack context contains nullptr");
  return packet_stack;
}

void ProtocolContext::PushParser(ApiParser& parser) {
  assert(!parsers_.full() &&
         "AE_API_PROTOCOL_MAX_PARSER_PACKER_DEPTH exceeded for parser");
  parsers_.push(&parser);
}

void ProtocolContext::PopParser() {
  assert(!parsers_.empty() && "Parser context is empty");
  parsers_.pop();
}

ApiParser* ProtocolContext::parser() {
  if (parsers_.empty()) {
    return nullptr;
  }
  auto* parser = parsers_.top();
  assert(parser != nullptr && "Parser context contains nullptr");
  return parser;
}

void ProtocolContext::PushPacker(ApiPacker& packer) {
  assert(!packers_.full() &&
         "AE_API_PROTOCOL_MAX_PARSER_PACKER_DEPTH exceeded for packer");
  packers_.push(&packer);
}

void ProtocolContext::PopPacker() {
  assert(!packers_.empty() && "Packer context is empty");
  packers_.pop();
}

ApiPacker* ProtocolContext::packer() {
  if (packers_.empty()) {
    return nullptr;
  }
  auto* packer = packers_.top();
  assert(packer != nullptr && "Packer context contains nullptr");
  return packer;
}

}  // namespace ae

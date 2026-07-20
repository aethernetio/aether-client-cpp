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

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <etl/generic_pool.h>

#include "aether/warning_disable.h"
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/stack.h>
#include <etl/vector.h>
DISABLE_WARNING_POP()

#include "aether/api_protocol/request_id.h"
#include "aether/config.h"

namespace ae {
class PacketStack;
class ApiParser;
class ApiPacker;

class ProtocolContext {
 public:
  class PendingResponse {
   public:
    virtual ~PendingResponse() = default;

    virtual void OnResult(ApiParser& parser) = 0;
    virtual void OnError(std::uint8_t error_type, std::int32_t error_code) = 0;
    virtual void OnEvicted() = 0;
  };

  static constexpr auto kMaxPendingResponses =
      AE_API_PROTOCOL_MAX_PENDING_RESPONSES;
  static constexpr auto kPendingResponseMaxSize =
      AE_API_PROTOCOL_PENDING_RESPONSE_MAX_SIZE;
  static constexpr auto kPendingResponseAlign =
      AE_API_PROTOCOL_PENDING_RESPONSE_ALIGN;
  static constexpr auto kMaxPacketStackDepth =
      AE_API_PROTOCOL_MAX_PACKET_STACK_DEPTH;
  static constexpr auto kMaxParserPackerDepth =
      AE_API_PROTOCOL_MAX_PARSER_PACKER_DEPTH;
  static_assert(kMaxPendingResponses > 0,
                "AE_API_PROTOCOL_MAX_PENDING_RESPONSES must be greater than 0");
  static_assert(
      kMaxPacketStackDepth > 0,
      "AE_API_PROTOCOL_MAX_PACKET_STACK_DEPTH must be greater than 0");
  static_assert(
      kMaxParserPackerDepth > 0,
      "AE_API_PROTOCOL_MAX_PARSER_PACKER_DEPTH must be greater than 0");

  ProtocolContext();
  ~ProtocolContext();

  template <typename Entry>
  Entry& CreatePendingResponse(RequestId request_id) {
    static_assert(sizeof(Entry) <= kPendingResponseMaxSize,
                  "Pending response entry exceeds "
                  "AE_API_PROTOCOL_PENDING_RESPONSE_MAX_SIZE");
    static_assert(alignof(Entry) <= kPendingResponseAlign,
                  "Pending response entry alignment exceeds "
                  "AE_API_PROTOCOL_PENDING_RESPONSE_ALIGN");

    PreparePendingResponseSlot(request_id);

    auto* entry_ptr = pending_response_pool_.template create<Entry>();
    assert(entry_ptr != nullptr &&
           "Pending response pool allocation failed after slot preparation");
    pending_responses_.emplace_back(PendingEntry{request_id, entry_ptr});
    return *entry_ptr;
  }

  void SetSendResultResponse(RequestId request_id);
  void SetSendErrorResponse(RequestId req_id, std::uint8_t error_type,
                            std::uint32_t error_code);

  void PushPacketStack(PacketStack& packet_stack);
  void PopPacketStack();
  PacketStack* packet_stack();

  void PushParser(ApiParser& parser);
  void PopParser();
  ApiParser* parser();

  void PushPacker(ApiPacker& packer);
  void PopPacker();
  ApiPacker* packer();

 private:
  using PendingResponsePool =
      etl::generic_pool<kPendingResponseMaxSize, kPendingResponseAlign,
                        kMaxPendingResponses>;

  struct PendingEntry {
    RequestId request_id;
    PendingResponse* response;
  };

  using PendingList = etl::vector<PendingEntry, kMaxPendingResponses>;
  using PacketStackStack = etl::stack<PacketStack*, kMaxPacketStackDepth>;
  using ParserStack = etl::stack<ApiParser*, kMaxParserPackerDepth>;
  using PackerStack = etl::stack<ApiPacker*, kMaxParserPackerDepth>;

  void EvictPending(PendingEntry const& entry);
  void DestroyPending(PendingEntry const& entry);
  void PreparePendingResponseSlot(RequestId request_id);
  PendingEntry TakePending(RequestId request_id);
  PendingEntry TakeOldestPending();

  PendingResponsePool pending_response_pool_;
  PendingList pending_responses_;

  PacketStackStack packet_stacks_;
  ParserStack parsers_;
  PackerStack packers_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_

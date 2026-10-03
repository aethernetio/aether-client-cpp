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

#ifndef AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_
#define AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_

#include <cassert>
#include <cstdint>

#include <etl/generic_pool.h>

#include "aether/warning_disable.h"
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/vector.h>
DISABLE_WARNING_POP()

#include "aether/config.h"

#include "aether/actions/action_context.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/api_protocol/details/api_promise.h"
#include "aether/api_protocol/details/request_id.h"

namespace ae {
class PacketReader;

class ProtocolContext {
 public:
  static constexpr auto kMaxPendingResponses =
      AE_API_PROTOCOL_MAX_PENDING_RESPONSES;
  static constexpr auto kFutureMaxSize = sizeof(ApiFuture<int>);
  static constexpr auto kFutureMaxAlign = alignof(ApiFuture<int>);

  static_assert(kMaxPendingResponses > 0,
                "AE_API_PROTOCOL_MAX_PENDING_RESPONSES must be greater than 0");

  explicit ProtocolContext(ActionContext auto const& context)
      : ProtocolContext{
            context.scheduler(),
            context.event_system(),
        } {}
  ProtocolContext(TaskScheduler& scheduler, EventSystem& event_system);
  ~ProtocolContext();

  TaskScheduler& scheduler() const;
  EventSystem& event_system() const;

  RequestId NextRequestId();

  // Create api future for specified value type with request id
  template <typename V>
  ApiFuture<V>& CreateFuture(RequestId request_id) {
    using Entry = ApiFuture<V>;
    static_assert(sizeof(Entry) <= kFutureMaxSize,
                  "Pending response entry exceeds kFutureMaxSize");
    static_assert(alignof(Entry) <= kFutureMaxAlign,
                  "Pending response entry alignment exceeds kFutureMaxAlign");

    PreparePendingResponseSlot();

    auto* entry_ptr = futures_pool_.template create<Entry>(*this, request_id);
    assert(entry_ptr != nullptr &&
           "Pending response pool allocation failed after slot preparation");
    pending_list_.emplace_back(PendingEntry{request_id, entry_ptr});
    return *entry_ptr;
  }

  bool SetSendResultResponse(PacketReader& reader, RequestId request_id);
  bool SetSendErrorResponse(RequestId req_id, std::uint8_t error_type,
                            std::int32_t error_code);

 private:
  using FuturesPool =
      etl::generic_pool<kFutureMaxSize, kFutureMaxAlign, kMaxPendingResponses>;

  struct PendingEntry {
    RequestId request_id;
    IApiFuture* response;
  };

  using PendingList = etl::vector<PendingEntry, kMaxPendingResponses>;

  // Call the eviction event on pending response
  void EvictPending(PendingEntry const& entry);
  // Call the destructor on pending response
  void DestroyPending(PendingEntry const& entry);
  // Ensure we could add at least one pending response, the oldest would be
  // evicted
  void PreparePendingResponseSlot();
  // Find pending entry with request id and pop it from pending list
  PendingEntry TakePending(RequestId request_id);
  // Pop the oldest pending response from the pending list
  PendingEntry TakeOldestPending();

  TaskScheduler* scheduler_;
  EventSystem* event_system_;
  std::uint32_t next_request_id_{};

  FuturesPool futures_pool_;
  PendingList pending_list_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_PROTOCOL_CONTEXT_H_

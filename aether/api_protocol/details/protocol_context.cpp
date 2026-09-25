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

#include "aether/api_protocol/details/protocol_context.h"

#include <cassert>
#include <cstdint>

#include "aether/tele.h"

namespace ae {
ProtocolContext::ProtocolContext(TaskScheduler& scheduler,
                                 EventSystem& event_system)
    : scheduler_{&scheduler}, event_system_{&event_system} {}

ProtocolContext::~ProtocolContext() {
  while (!pending_list_.empty()) {
    auto entry = TakeOldestPending();
    DestroyPending(entry);
  }
}

TaskScheduler& ProtocolContext::scheduler() const { return *scheduler_; }
EventSystem& ProtocolContext::event_system() const { return *event_system_; }

RequestId ProtocolContext::NextRequestId() {
  return RequestId{next_request_id_++};
}

bool ProtocolContext::SetSendResultResponse(PacketReader& reader,
                                            RequestId request_id) {
  auto entry = TakePending(request_id);
  if (entry.response == nullptr) {
    AE_TELED_DEBUG("No callback for request id {} cancel parse", request_id);
    return false;
  }

  entry.response->OnResult(reader);
  DestroyPending(entry);
  return true;
}

bool ProtocolContext::SetSendErrorResponse(RequestId req_id,
                                           std::uint8_t error_type,
                                           std::int32_t error_code) {
  auto entry = TakePending(req_id);
  if (entry.response == nullptr) {
    AE_TELED_DEBUG("No callback for error with request id {}", req_id);
    return false;
  }

  entry.response->OnError(error_type, error_code);
  DestroyPending(entry);
  return true;
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
  futures_pool_.destroy(entry.response);
}

void ProtocolContext::PreparePendingResponseSlot() {
  // ensure there is enough in pool for new pending response
  // oldest pending should be evicted
  // If during eviction somebody take free space, evict again
  while (pending_list_.full()) {
    auto oldest_entry = TakeOldestPending();
    EvictPending(oldest_entry);
  }
}

ProtocolContext::PendingEntry ProtocolContext::TakePending(
    RequestId request_id) {
  auto* it = std::find_if(std::begin(pending_list_), std::end(pending_list_),
                          [request_id](auto const& pe) noexcept {
                            return pe.request_id == request_id;
                          });
  if (it == std::end(pending_list_)) {
    return PendingEntry{RequestId{}, nullptr};
  }

  auto found = *it;
  pending_list_.erase(it);
  return found;
}

ProtocolContext::PendingEntry ProtocolContext::TakeOldestPending() {
  assert(!pending_list_.empty() &&
         "TakeOldestPending requires a pending response");

  // oldest is the first
  auto* it = pending_list_.begin();
  auto found = *it;
  pending_list_.erase(it);

  return found;
}
}  // namespace ae

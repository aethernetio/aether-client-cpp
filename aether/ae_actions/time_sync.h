/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_AE_ACTIONS_TIME_SYNC_H_
#define AETHER_AE_ACTIONS_TIME_SYNC_H_

#include "aether/config.h"

#if AE_TIME_SYNC_ENABLED

#  include <optional>
#  include "aether/env.h"
#  include "aether/clock.h"
#  include "aether/ae_context.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/executors/executors.h"

namespace ae {
class Client;

namespace time_sync_internal {
struct Success {};
struct Failed {};
struct Retry {};

class TimeSyncRequest : public Action {
 public:
  TimeSyncRequest(AeContext const& ae_context, Ptr<Client> const& client);

 private:
  auto EnsureConnected();
  auto SyncRequest();
  static void HandleResponse(std::chrono::milliseconds server_epoch,
                             TimePoint request_time, TimePoint response_time);

  AeContext ae_context_;
  PtrView<Client> client_;
  TimePoint request_time_;
  Subscription link_state_sub_;
  Subscription response_sub_;
  Subscription write_action_sub_;
  std::optional<
      ex::AnyWaiter<ex::set_value_t(Success), ex::set_error_t(Failed)>>
      waiter_;
};
}  // namespace time_sync_internal

class TimeSyncAction {
  enum class State : char {
    kMakeRequest,
    kWaitInterval,
    kFailed,
  };

 public:
  TimeSyncAction(AeContext const& ae_context, Ptr<Client> const& client,
                 Duration sync_interval);

 private:
  void MakeRequest();
  void ScheduleNextSync();

  AeContext ae_context_;
  PtrView<Client> client_;
  Duration sync_interval_;
  std::optional<time_sync_internal::TimeSyncRequest> time_sync_request_;
  TaskSubscription task_sub_;

  static RTC_STORAGE_ATTR TimePoint last_sync_time;
};
}  // namespace ae
#endif
#endif  // AETHER_AE_ACTIONS_TIME_SYNC_H_

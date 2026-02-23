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

#  include "aether/env.h"
#  include "aether/clock.h"
#  include "aether/client.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/types/state_machine.h"
#  include "aether/events/event_subscription.h"

namespace ae {
class TimeSyncAction : public Action<TimeSyncAction> {
  enum class State : char {
    kEnsureConnected,
    kMakeRequest,
    kWaitResponse,
    kWaitInterval,
    kFailed,
  };

  static constexpr auto kRequestTimeout = 10s;

 public:
  TimeSyncAction(ActionContext action_context, Ptr<Client> const& client,
                 Duration sync_interval);

  UpdateStatus Update();

 private:
  void EnsureConnected();
  void SyncRequest();
  static void HandleResponse(std::chrono::milliseconds server_epoch,
                             TimePoint request_time, TimePoint response_time);
  UpdateStatus WaitResponse();
  UpdateStatus WaitInterval();

  PtrView<Client> client_;
  Duration sync_interval_;
  StateMachine<State> state_;
  Subscription link_state_sub_;
  Subscription write_action_sub_;
  Subscription response_sub_;

  static RTC_STORAGE_ATTR SystemTimePoint last_sync_time;
};
}  // namespace ae
#endif
#endif  // AETHER_AE_ACTIONS_TIME_SYNC_H_

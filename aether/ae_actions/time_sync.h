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
#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/types/state_machine.h"

namespace ae {
class Aether;
class Client;
namespace time_sync_internal {
class TimeSyncRequest;
}

class TimeSyncAction : public Action<TimeSyncAction> {
  enum class State : char {
    kMakeRequest,
    kWaitInterval,
    kFailed,
  };

 public:
  TimeSyncAction(ActionContext action_context, Ptr<Aether> const& aether,
                 Ptr<Client> const& client, Duration sync_interval);

  UpdateStatus Update();

 private:
  void MakeRequest();
  UpdateStatus WaitInterval();

  ActionContext action_context_;
  PtrView<Aether> aether_;
  PtrView<Client> client_;
  Duration sync_interval_;
  StateMachine<State> state_;
  ActionPtr<time_sync_internal::TimeSyncRequest> time_sync_request_;

  static RTC_STORAGE_ATTR SystemTimePoint last_sync_time;
};
}  // namespace ae
#endif
#endif  // AETHER_AE_ACTIONS_TIME_SYNC_H_

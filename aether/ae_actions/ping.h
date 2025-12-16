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

#ifndef AETHER_AE_ACTIONS_PING_H_
#define AETHER_AE_ACTIONS_PING_H_

#include <cstdint>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/etl/include/etl/circular_buffer.h"
DISABLE_WARNING_POP()

#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"
#include "aether/api_protocol/request_id.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

namespace ae {
class Channel;
class ClientServerConnection;

class Ping : public Action<Ping> {
  enum class State : std::uint8_t {
    kSendPing,
    kWaitResponse,
    kWaitInterval,
    kStopped,
    kError,
  };

  static constexpr std::uint8_t kMaxStorePingTimes = 10;

  struct PingRequest {
    TimePoint start;
    TimePoint expected_end;
    RequestId request_id;
  };

 public:
  Ping(ActionContext action_context, Ptr<Channel> const& channel,
       ClientServerConnection& client_server_connection,
       Duration ping_interval);

  AE_CLASS_NO_COPY_MOVE(Ping);

  UpdateStatus Update();
  void Stop();

 private:
  void SendPing();
  TimePoint WaitInterval();
  TimePoint WaitResponse();
  void PingResponse(RequestId request_id);

  PtrView<Channel> channel_;
  ClientServerConnection* client_server_connection_;
  Duration ping_interval_;

  etl::circular_buffer<PingRequest, kMaxStorePingTimes> ping_requests_;

  Subscription write_subscription_;
  MultiSubscription wait_responses_;
  StateMachine<State> state_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_PING_H_

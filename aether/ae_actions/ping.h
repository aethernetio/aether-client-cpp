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
#include "aether/server.h"
#include "aether/channel.h"
#include "aether/ptr/ptr.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"
#include "aether/client_connections/client_to_server_stream.h"

#include "aether/methods/client_api/client_safe_api.h"

namespace ae {
class Ping : public Action<Ping> {
  enum class State : std::uint8_t {
    kWaitLink,
    kSendPing,
    kWaitResponse,
    kWaitInterval,
    kError,
  };

  static constexpr std::uint8_t kMaxRepeatPingCount = 5;
  static constexpr std::uint8_t kMaxStorePingTimes = kMaxRepeatPingCount * 2;

 public:
  Ping(ActionContext action_context, Server::ptr const& server,
       Channel::ptr const& channel,
       ClientToServerStream& client_to_server_stream, Duration ping_interval);
  ~Ping() override;

  AE_CLASS_NO_COPY_MOVE(Ping);

  TimePoint Update(TimePoint current_time) override;

 private:
  void SendPing(TimePoint current_time);
  TimePoint WaitInterval(TimePoint current_time);
  TimePoint WaitResponse(TimePoint current_time);
  void PingResponse(RequestId request_id);

  PtrView<Server> server_;
  PtrView<Channel> channel_;
  ClientToServerStream* client_to_server_stream_;
  Duration ping_interval_;

  std::size_t repeat_count_;
  etl::circular_buffer<std::pair<RequestId, TimePoint>, kMaxStorePingTimes>
      ping_times_;

  Subscription write_subscription_;
  MultiSubscription wait_responses_;
  StateMachine<State> state_;
  Subscription state_changed_sub_;
  Subscription stream_changed_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_PING_H_

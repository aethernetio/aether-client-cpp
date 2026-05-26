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
#include <optional>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/circular_buffer.h>
DISABLE_WARNING_POP()

#include "aether/common.h"
#include "aether/ptr/ptr.h"
#include "aether/ae_context.h"
#include "aether/ptr/ptr_view.h"
#include "aether/events/events.h"
#include "aether/api_protocol/request_id.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

namespace ae {
class Channel;
class ClientServerConnection;

class Ping {
  static constexpr std::uint8_t kMaxStorePingTimes = 10;

  struct PingRequest {
    TimePoint start;
    RequestId request_id;
  };

 public:
  using PingFailed = Event<void()>;

  Ping(AeContext const& ae_context, Ptr<Channel> const& channel,
       ClientServerConnection& client_server_connection,
       Duration ping_interval);

  AE_CLASS_NO_COPY_MOVE(Ping);

  PingFailed::Subscriber ping_failed();

 private:
  void SendPing();
  TimePoint WaitInterval();
  TimePoint WaitResponse();
  void PingResponse(RequestId request_id);
  void PingResponseTimeout(RequestId request_id);

  AeContext ae_context_;
  PtrView<Channel> channel_;
  ClientServerConnection* client_server_connection_;
  Duration ping_interval_;

  etl::circular_buffer<std::optional<PingRequest>, kMaxStorePingTimes>
      ping_requests_;

  PingFailed ping_failed_;
  MultiSubscription write_subs_;
  MultiSubscription wait_responses_;
  TaskSubscription schedule_sub_;
  TaskSubscription timeout_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_PING_H_

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

#include "aether/config.h"

#if AE_ENABLE_PING

#  include <cstdint>
#  include <optional>

#  include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#  include <etl/circular_buffer.h>
DISABLE_WARNING_POP()

#  include "aether-miscpp/types/result.h"

#  include "aether/ae_context.h"
#  include "aether/events/events.h"
#  include "aether/types/server_id.h"
#  include "aether/api_protocol/request_id.h"
#  include "aether/events/event_subscription.h"

namespace ae {
class Channel;
class CloudServerConnection;

class Ping {
  static constexpr std::uint8_t kMaxStorePingTimes = 10;

  struct PingRequest {
    TimePoint start;
    RequestId request_id;
    Subscription wait_result_sub;
    TaskSubscription timeout_sub;
    Subscription write_sub;
  };

 public:
  using ResultEvent = Event<void(Result<Duration, int>)>;

  Ping(AeContext const& ae_context,
       CloudServerConnection& cloud_server_connection, Duration ping_interval,
       Duration rx_window, Duration timeout);

  AE_CLASS_NO_COPY_MOVE(Ping);

  ResultEvent::Subscriber result_event();

  void SetTimeout(Duration timeout);

 private:
  void ScheduleFirstPing();
  void SendPing();
  TimePoint WaitInterval();
  TimePoint WaitResponse();
  void PingResponse(RequestId request_id);
  void PingResponseTimeout(RequestId request_id);

  AeContext ae_context_;
  CloudServerConnection* cloud_server_connection_;
  Duration ping_interval_;
  Duration rx_window_;
  Duration timeout_;
  ServerId server_id_;

  etl::circular_buffer<std::optional<PingRequest>, kMaxStorePingTimes>
      ping_requests_;

  ResultEvent result_event_;
  Subscription link_state_sub_;
  TaskSubscription schedule_sub_;
};
}  // namespace ae
#endif  // AE_ENABLE_PING
#endif  // AETHER_AE_ACTIONS_PING_H_

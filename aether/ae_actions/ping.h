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

#  include "aether-miscpp/types/result.h"

#  include "aether/ae_context.h"
#  include "aether/api_protocol/request_id.h"
#  include "aether/events/event_subscription.h"
#  include "aether/events/events.h"
#  include "aether/tasks/details/task_subsctiption.h"
#  include "aether/types/server_id.h"

namespace ae {
class Channel;
class CloudServerConnection;

class Ping {
 public:
  using ResultEvent = Event<void(Result<Duration, int>)>;

  Ping(AeContext const& ae_context,
       CloudServerConnection& cloud_server_connection, Duration next_ping_hint,
       Duration rx_window, Duration timeout);

  AE_CLASS_NO_COPY_MOVE(Ping);

  ResultEvent::Subscriber result_event();

  void Start(TimePoint current_time);

 private:
  void PingResponse(RequestId request_id);
  void PingResponseError(RequestId request_id, std::int32_t error_code);
  void PingResponseTimeout(RequestId request_id);
  void ResetRequestSubscriptions();

  AeContext ae_context_;
  CloudServerConnection* cloud_server_connection_;
  Duration next_ping_hint_;
  Duration rx_window_;
  Duration timeout_;
  ServerId server_id_;

  TimePoint request_start_;
  Subscription wait_result_sub_;
  TaskSubscription timeout_sub_;
  Subscription write_sub_;

  ResultEvent result_event_;
  bool started_{};
};
}  // namespace ae
#endif  // AE_ENABLE_PING
#endif  // AETHER_AE_ACTIONS_PING_H_

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

#ifndef AETHER_AE_ACTIONS_PROBE_PING_H_
#define AETHER_AE_ACTIONS_PROBE_PING_H_

#include "aether/config.h"

#if AE_ENABLE_PING

#  include <cstdint>
#  include <variant>

#  include "aether-miscpp/types/result.h"

#  include "aether/ae_context.h"
#  include "aether/api_protocol/request_id.h"
#  include "aether/clock.h"
#  include "aether/events/event_subscription.h"
#  include "aether/events/events.h"
#  include "aether/tasks/details/task_subsctiption.h"
#  include "aether/types/server_id.h"

namespace ae {
class CloudServerConnection;

// Immediate Æther probe using AuthorizedApi::probe_ping (method 43).
// RTT is recorded into ChannelStatistics by the caller (same as Ping).
class ProbePing {
 public:
  struct LateDuration {
    Duration duration;
  };
  using ProbePingResult = std::variant<Ok<Duration>, LateDuration, Error<int>>;
  using ResultEvent = Event<void(ProbePingResult const&)>;

  ProbePing(AeContext const& ae_context,
            CloudServerConnection& cloud_server_connection,
            std::uint64_t probe_id, Duration current_rx_window,
            Duration next_rx_delay, Duration next_rx_window, Duration timeout);

  AE_CLASS_NO_COPY_MOVE(ProbePing);

  ResultEvent::Subscriber result_event();
  void Start(TimePoint current_time);

 private:
  void OnOk(RequestId request_id);
  void OnError(RequestId request_id, std::int32_t error_code);
  void OnTimeout(RequestId request_id);
  void ResetSubs();

  enum class RequestState : char {
    kCreated,
    kPending,
    kTimedOut,
    kFinished,
  };

  AeContext ae_context_;
  CloudServerConnection* cloud_server_connection_;
  std::uint64_t probe_id_;
  Duration current_rx_window_;
  Duration next_rx_delay_;
  Duration next_rx_window_;
  Duration timeout_;
  ServerId server_id_{};

  TimePoint request_start_{};
  Subscription wait_result_sub_;
  TaskSubscription timeout_sub_;
  Subscription write_sub_;
  ResultEvent result_event_;
  RequestState state_{RequestState::kCreated};
};
}  // namespace ae
#endif  // AE_ENABLE_PING
#endif  // AETHER_AE_ACTIONS_PROBE_PING_H_

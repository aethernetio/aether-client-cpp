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

#include "aether/ae_actions/ping.h"
#if AE_ENABLE_PING

#  include <optional>

#  include "aether/server.h"
#  include "aether/cloud_connections/cloud_server_connection.h"
#  include "aether/server_connections/client_server_connection.h"
#  include "aether/work_cloud_api/work_server_api/authorized_api.h"

#  include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Ping::Ping(AeContext const& ae_context,
           CloudServerConnection& cloud_server_connection,
           Duration ping_interval, Duration rx_window, Duration timeout)
    : ae_context_{ae_context},
      cloud_server_connection_{&cloud_server_connection},
      ping_interval_{ping_interval},
      rx_window_{rx_window},
      timeout_{timeout},
      server_id_{cloud_server_connection_->server()->server_id} {
  AE_TELE_INFO(
      kPing,
      "Ping action created to server id: {}, interval: {:%S}s, rx_window: "
      "{:%S}s, timeout: {:%S}s",
      server_id_, ping_interval_, rx_window_, timeout_);

  ScheduleFirstPing();
}

Ping::ResultEvent::Subscriber Ping::result_event() { return result_event_; }

void Ping::SetTimeout(Duration timeout) {
  // Only next ping will use new timeout
  timeout_ = timeout;
}

void Ping::ScheduleFirstPing() {
  // TODO: calculate actual next ping time
  auto* cc = cloud_server_connection_->client_connection();
  assert(cc != nullptr && "Client connection is null");

  // send first ping only after client connection is fully linked
  if (cc->stream_info().link_state == LinkState::kLinked) {
    // send ping on the next tick
    schedule_sub_ = ae_context_.scheduler().Task([&]() { SendPing(); });
  } else {
    link_state_sub_ = cc->stream_update_event().Subscribe([this, cc]() {
      if (cc->stream_info().link_state == LinkState::kLinked) {
        link_state_sub_.Reset();
        // send ping on the next tick
        schedule_sub_ = ae_context_.scheduler().Task([&]() { SendPing(); });
      }
    });
  }
}

void Ping::SendPing() {
  AE_TELE_DEBUG(kPingSend, "Send ping");

  auto& write_action =
      cloud_server_connection_->client_connection()->AuthorizedApiCall(
          SubApi{[this](ApiContext<AuthorizedApi>& auth_api) {
            auto ping_interval_u64 = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    ping_interval_)
                    .count());
            auto rx_window_u64 = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    rx_window_)
                    .count());

            auto& pong_promise =
                auth_api->ping(ping_interval_u64, rx_window_u64);
            auto req_id = pong_promise.request_id();

            // save the ping request
            AE_TELED_DEBUG("Ping server id {}, request {} expected time {:%S}s",
                           server_id_, req_id, timeout_);
            auto current_time = Now();
            auto end_time = current_time + timeout_;

            ping_requests_.push(PingRequest{
                .start = current_time,
                .request_id = req_id,
                // Wait for response
                .wait_result_sub = pong_promise.Subscribe(
                    [&, req_id](auto&&...) { PingResponse(req_id); }),
                .timeout_sub = ae_context_.scheduler().DelayedTask(
                    [this, req_id]() { PingResponseTimeout(req_id); },
                    end_time),
                .write_sub = {},
            });
          }});

  auto& req = ping_requests_.back();
  assert(req.has_value() &&
         "After call AuthorizedApiCall ping request should be saved");

  req->write_sub = write_action.status_event().Subscribe([&](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELE_ERROR(kPingWriteError, "Ping write error");
      result_event_.Emit(Error{1});
    }
  });

  // setup next ping interval
  schedule_sub_ = ae_context_.scheduler().DelayedTask([this]() { SendPing(); },
                                                      ping_interval_);
}

void Ping::PingResponse(RequestId request_id) {
  auto request_it = std::find_if(
      std::begin(ping_requests_), std::end(ping_requests_),
      [&](auto const& p) { return p && (p->request_id == request_id); });

  if (request_it == std::end(ping_requests_)) {
    AE_TELED_WARNING("Got lost, or not our pong response");
    return;
  }

  auto& request = *request_it;

  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - request->start);

  // reset request as finished
  request.reset();

  AE_TELED_DEBUG("Ping server id {} request {} received by {:%S} s", server_id_,
                 request_id, ping_duration);
  result_event_.Emit(Ok{ping_duration});
}

void Ping::PingResponseTimeout(RequestId request_id) {
  auto request_it = std::find_if(
      std::begin(ping_requests_), std::end(ping_requests_),
      [&](auto const& p) { return p && (p->request_id == request_id); });

  if (request_it == std::end(ping_requests_)) {
    AE_TELED_WARNING("Timeout for lost, or not our pong response");
    return;
  }

  request_it->reset();
  AE_TELE_ERROR(kPingTimeout, "Ping server id {} request {} timeout",
                server_id_, request_id);
  result_event_.Emit(Error{2});
}

}  // namespace ae
#endif  // AE_ENABLE_PING

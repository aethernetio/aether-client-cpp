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

#include <optional>

#include "aether/channels/channel.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Ping::Ping(AeContext const& ae_context, Ptr<Channel> const& channel,
           ClientServerConnection& client_server_connection,
           Duration ping_interval)
    : ae_context_{ae_context},
      channel_{channel},
      client_server_connection_{&client_server_connection},
      ping_interval_{ping_interval},
      alive_ctx_{ae_context_, this} {
  AE_TELE_INFO(kPing, "Ping action created, interval {:%S}s", ping_interval);
  // send ping on the next tick
  ae_context_.scheduler().Task([&]() { SendPing(); });
}

Ping::PingFailed::Subscriber Ping::ping_failed() {
  return EventSubscriber{ping_failed_};
}

void Ping::SendPing() {
  AE_TELE_DEBUG(kPingSend, "Send ping");

  auto& write_action = client_server_connection_->AuthorizedApiCall(
      SubApi{[this](ApiContext<AuthorizedApi>& auth_api) {
        auto ping_interval_u64 = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                ping_interval_)
                .count());
        auto& pong_promise = auth_api->ping(ping_interval_u64);
        auto req_id = pong_promise.request_id();

        // save the ping request

        auto channel_ptr = channel_.Lock();
        assert(channel_ptr);
        auto expected_ping_time = channel_ptr->ResponseTimeout();
        auto current_time = Now();
        auto end_time = current_time + expected_ping_time;
        AE_TELED_DEBUG("Ping request expected time {:%S}s", expected_ping_time);

        ping_requests_.push(PingRequest{
            current_time,
            req_id,
        });

        // Wait for response
        wait_responses_ += pong_promise.Subscribe(
            [&, req_id](auto&&...) { PingResponse(req_id); });

        // setup response timeout
        ae_context_.scheduler().DelayedTask(
            [imalive{alive_ctx_.View()}, req_id]() {
              if (!imalive) {
                return;
              }
              imalive->PingResponseTimeout(req_id);
            },
            end_time);
      }});

  write_subscription_ = write_action.status_event().Subscribe([&](auto status) {
    if (status == WriteAction::Status::kFail) {
      AE_TELE_ERROR(kPingWriteError, "Ping write error");
      ping_failed_.Emit();
    }
  });

  // setup ping interval
  ae_context_.scheduler().DelayedTask(
      [imalive{alive_ctx_.View()}]() {
        if (imalive) {
          imalive->SendPing();
        }
      },
      ping_interval_);
}

void Ping::PingResponse(RequestId request_id) {
  auto request_it = std::find_if(
      std::begin(ping_requests_), std::end(ping_requests_),
      [&](auto const& p) { return p && (p->request_id == request_id); });

  if (request_it == std::end(ping_requests_)) {
    AE_TELED_DEBUG("Got lost, or not our pong response");
    return;
  }

  auto& request = *request_it;

  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - request->start);

  AE_TELED_DEBUG("Ping received by {:%S} s", ping_duration);
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);
  channel_ptr->channel_statistics().AddResponseTime(ping_duration);

  // reset request as finished
  request.reset();
}

void Ping::PingResponseTimeout(RequestId request_id) {
  for (auto& p : ping_requests_) {
    if (p && (p->request_id == request_id)) {
      p.reset();
      // timeout
      AE_TELE_ERROR(kPingTimeout, "Ping timeout");
      ping_failed_.Emit();
    }
  }
}

}  // namespace ae

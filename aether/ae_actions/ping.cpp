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
Ping::Ping(ActionContext action_context, Ptr<Channel> const& channel,
           ClientServerConnection& client_server_connection,
           Duration ping_interval)
    : Action{action_context},
      channel_{channel},
      client_server_connection_{&client_server_connection},
      ping_interval_{ping_interval},
      state_{State::kSendPing} {
  AE_TELE_INFO(kPing, "Ping action created, interval {:%S}s", ping_interval);

  state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

UpdateStatus Ping::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSendPing:
        SendPing();
        break;
      case State::kWaitResponse:
      case State::kWaitInterval:
        break;
      case State::kStopped:
        return UpdateStatus::Stop();
      case State::kError:
        return UpdateStatus::Error();
    }
  }

  if (state_.get() == State::kWaitResponse) {
    return UpdateStatus::Delay(WaitResponse());
  }
  if (state_.get() == State::kWaitInterval) {
    return UpdateStatus::Delay(WaitInterval());
  }

  return {};
}

void Ping::Stop() { state_ = State::kStopped; }

void Ping::SendPing() {
  AE_TELE_DEBUG(kPingSend, "Send ping");

  auto write_action = client_server_connection_->AuthorizedApiCall(
      SubApi{[this](ApiContext<AuthorizedApi>& auth_api) {
        auto pong_promise = auth_api->ping(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                ping_interval_)
                .count()));
        // save the ping request
        auto current_time = Now();
        auto channel_ptr = channel_.Lock();
        assert(channel_ptr);
        auto expected_ping_time = channel_ptr->ResponseTimeout();
        AE_TELED_DEBUG("Ping request expected time {:%S}s", expected_ping_time);

        ping_requests_.push(PingRequest{
            current_time,
            current_time + expected_ping_time,
            pong_promise->request_id(),
        });
        // Wait for response
        wait_responses_.Push(pong_promise->StatusEvent().Subscribe(OnResult{
            [&](auto const& promise) { PingResponse(promise.request_id()); }}));
      }});

  write_subscription_ = write_action->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELE_ERROR(kPingWriteError, "Ping write error");
    state_ = State::kError;
  }});

  state_ = State::kWaitResponse;
}

TimePoint Ping::WaitInterval() {
  auto current_time = Now();
  auto const& ping_time = ping_requests_.back().start;
  if ((ping_time + ping_interval_) > current_time) {
    return ping_time + ping_interval_;
  }
  state_ = State::kSendPing;
  return current_time;
}

TimePoint Ping::WaitResponse() {
  auto current_time = Now();

  auto const& expected_ping_time = ping_requests_.back().expected_end;
  if (expected_ping_time > current_time) {
    return expected_ping_time;
  }
  // timeout
  AE_TELE_ERROR(kPingTimeout, "Ping timeout");
  state_ = State::kError;

  return current_time;
}

void Ping::PingResponse(RequestId request_id) {
  auto request_time = std::invoke([&]() -> std::optional<TimePoint> {
    for (auto const& [time, _, req_id] : ping_requests_) {
      if (req_id == request_id) {
        return time;
      }
    }
    return std::nullopt;
  });

  if (!request_time) {
    AE_TELED_DEBUG("Got lost, or not our pong response");
    return;
  }

  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - *request_time);

  AE_TELED_DEBUG("Ping received by {:%S} s", ping_duration);
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);
  channel_ptr->channel_statistics().AddResponseTime(ping_duration);

  state_ = State::kWaitInterval;
}

}  // namespace ae

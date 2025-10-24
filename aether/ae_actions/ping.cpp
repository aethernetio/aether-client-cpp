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

#include <utility>
#include <optional>

#include "aether/server_connections/client_server_connection.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Ping::Ping(ActionContext action_context, Server::ptr const& server,
           Channel::ptr const& channel,
           ClientServerConnection& client_server_connection,
           Duration ping_interval)
    : Action{action_context},
      server_{server},
      channel_{channel},
      client_server_connection_{&client_server_connection},
      ping_interval_{ping_interval},
      repeat_count_{},
      state_{State::kSendPing},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_INFO(kPing, "Ping action created, interval {:%S}", ping_interval);
}

Ping::~Ping() = default;

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

        ping_times_.push(std::make_pair(pong_promise->request_id(), Now()));
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
  auto const& ping_time = ping_times_.back().second;
  if ((ping_time + ping_interval_) > current_time) {
    return ping_time + ping_interval_;
  }
  state_ = State::kSendPing;
  return current_time;
}

TimePoint Ping::WaitResponse() {
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);
  auto current_time = Now();
  auto expected_ping_time = channel_ptr->ResponseTimeout();
  auto timeout = expected_ping_time;

  auto const& ping_time = ping_times_.back().second;
  if ((ping_time + timeout) > current_time) {
    return ping_time + timeout;
  }
  // timeout
  AE_TELE_INFO(kPingTimeout, "Timeout is {:%S}", timeout);
  if (repeat_count_ >= kMaxRepeatPingCount) {
    AE_TELE_ERROR(kPingTimeoutError, "Ping repeat count exceeded");
    state_ = State::kError;
  } else {
    repeat_count_++;
    state_ = State::kSendPing;
  }

  return current_time;
}

void Ping::PingResponse(RequestId request_id) {
  auto request_time = [&]() -> std::optional<TimePoint> {
    for (auto const& [req_id, time] : ping_times_) {
      if (req_id == request_id) {
        return time;
      }
    }
    return std::nullopt;
  }();

  if (!request_time) {
    AE_TELED_DEBUG("Got lost, or not our ping request");
    return;
  }

  repeat_count_ = 0;
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

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

#include "aether/api_protocol/packet_builder.h"
#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/ae_actions/ae_actions_tele.h"

namespace ae {
Ping::Ping(ActionContext action_context, Server::ptr const& server,
           Channel::ptr const& channel,
           ClientToServerStream& client_to_server_stream,
           Duration ping_interval)
    : Action{action_context},
      server_{server},
      channel_{channel},
      client_to_server_stream_{&client_to_server_stream},
      ping_interval_{ping_interval},
      repeat_count_{},
      state_{State::kWaitLink},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })},
      stream_changed_sub_{
          client_to_server_stream_->stream_update_event().Subscribe([this]() {
            if (client_to_server_stream_->stream_info().is_linked) {
              state_ = State::kSendPing;
            }
          })} {
  AE_TELE_INFO(kPing, "Ping action created, interval {:%S}", ping_interval);
}

Ping::~Ping() = default;

ActionResult Ping::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitLink:
        break;
      case State::kSendPing:
        SendPing();
        break;
      case State::kWaitResponse:
      case State::kWaitInterval:
        break;
      case State::kError:
        return ActionResult::Error();
    }
  }

  if (state_.get() == State::kWaitResponse) {
    return ActionResult::Delay(WaitResponse());
  }
  if (state_.get() == State::kWaitInterval) {
    return ActionResult::Delay(WaitInterval());
  }

  return {};
}

void Ping::SendPing() {
  AE_TELE_DEBUG(kPingSend, "Send ping");

  auto api_adapter = client_to_server_stream_->authorized_api_adapter();
  auto pong_promise = api_adapter->ping(static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(ping_interval_)
          .count()));

  ping_times_.push(std::make_pair(pong_promise->request_id(), Now()));
  // Wait for response
  wait_responses_.Push(pong_promise->ResultEvent().Subscribe(
      [&](auto const& promise) { PingResponse(promise.request_id()); }));

  auto write_action = api_adapter.Flush();
  write_subscription_ =
      write_action->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELE_ERROR(kPingWriteError, "Ping write error");
        state_ = State::kError;
      });

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
  auto timeout = channel_ptr->expected_ping_time();

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
  channel_ptr->AddPingTime(ping_duration);

  state_ = State::kWaitInterval;
}

}  // namespace ae

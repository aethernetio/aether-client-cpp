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
           Channel::ptr const& channel, ByteStream& server_stream,
           Duration ping_interval)
    : Action{action_context},
      server_{server},
      channel_{channel},
      server_stream_{&server_stream},
      ping_interval_{ping_interval},
      read_client_safe_api_gate_{protocol_context_, client_safe_api_},
      repeat_count_{},
      state_{State::kWaitLink},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })},
      stream_changed_sub_{
          server_stream_->in().gate_update_event().Subscribe([this]() {
            if (read_client_safe_api_gate_.stream_info().is_linked) {
              state_ = State::kSendPing;
            }
          })} {
  AE_TELE_INFO(kPing, "Ping action created, interval {:%S}", ping_interval);
  Tie(read_client_safe_api_gate_, *server_stream_);
}

Ping::~Ping() = default;

TimePoint Ping::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitLink:
        break;
      case State::kSendPing:
        SendPing(current_time);
        break;
      case State::kWaitResponse:
      case State::kWaitInterval:
        break;
      case State::kError:
        Action::Error(*this);
        return current_time;
    }
  }

  if (state_.get() == State::kWaitResponse) {
    return WaitResponse(current_time);
  }
  if (state_.get() == State::kWaitInterval) {
    return WaitInterval(current_time);
  }

  return current_time;
}

void Ping::SendPing(TimePoint current_time) {
  AE_TELE_DEBUG(kPingSend, "Send ping");
  auto request_id = RequestId::GenRequestId();
  ping_times_.push(std::make_pair(request_id, current_time));
  auto packet = PacketBuilder{
      protocol_context_,
      PackMessage{AuthorizedApi{},
                  AuthorizedApi::Ping{
                      {},
                      request_id,
                      static_cast<std::uint64_t>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              ping_interval_)
                              .count())}}};
  auto write_action =
      server_stream_->in().Write(std::move(packet), current_time);

  write_subscription_ = write_action->SubscribeOnError([this](auto const&) {
    AE_TELE_ERROR(kPingWriteError, "Ping write error");
    state_ = State::kError;
  });

  // Wait for response
  SendResult::OnResponse(
      protocol_context_, request_id,
      [&, req_id{request_id}](ApiParser&) { PingResponse(req_id); });

  state_ = State::kWaitResponse;
}

TimePoint Ping::WaitInterval(TimePoint current_time) {
  auto const& ping_time = ping_times_.back().second;
  if ((ping_time + ping_interval_) > current_time) {
    return ping_time + ping_interval_;
  }
  state_ = State::kSendPing;
  return current_time;
}

TimePoint Ping::WaitResponse(TimePoint current_time) {
  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);
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

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
      case State::kWaitResponse:  // TODO: response timeout and response
                                  // statistics
      case State::kWaitInterval:
        break;
      case State::kError:
        Action::Error(*this);
        return current_time;
    }
  }

  if (state_.get() == State::kWaitInterval) {
    if ((last_ping_time_ + ping_interval_) <= current_time) {
      state_ = State::kSendPing;
    } else {
      return last_ping_time_ + ping_interval_;
    }
  }

  return current_time;
}

void Ping::SendPing(TimePoint current_time) {
  AE_TELE_DEBUG(kPingSend, "Send ping");
  last_ping_time_ = current_time;
  auto request_id = RequestId::GenRequestId();
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
  SendResult::OnResponse(protocol_context_, request_id,
                         [&](ApiParser&) { PingResponse(); });

  state_ = State::kWaitResponse;
}

void Ping::PingResponse() {
  auto current_time = Now();
  auto ping_duration =
      std::chrono::duration_cast<Duration>(current_time - last_ping_time_);

  AE_TELED_DEBUG("Ping received by {:%S} s", ping_duration);
  state_ = State::kWaitInterval;
}

}  // namespace ae

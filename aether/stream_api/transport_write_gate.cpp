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

#include "aether/stream_api/transport_write_gate.h"

#include <utility>

#include "aether/actions/action.h"
#include "aether/stream_api/istream.h"

#include "aether/tele/tele.h"

namespace ae {

TransportWriteGate::TransportStreamWriteAction::TransportStreamWriteAction(
    ActionContext action_context,
    ActionView<PacketSendAction> packet_send_action)
    : StreamWriteAction(action_context),
      packet_send_action_{std::move(packet_send_action)} {
  subscriptions_.Push(
      packet_send_action_->SubscribeOnResult([this](auto const& /* action */) {
        state_ = State::kDone;
        Action::Result(*this);
      }),
      packet_send_action_->SubscribeOnError([this](auto const& action) {
        switch (action.state()) {
          case PacketSendAction::State::kTimeout:
            state_ = State::kTimeout;
            break;
          case PacketSendAction::State::kFailed:
            state_ = State::kFailed;
            break;
          default:
            AE_TELED_ERROR("What kind of error is this?");
            assert(false);
        }
        Action::Error(*this);
      }),
      packet_send_action_->SubscribeOnStop([this](auto const&) {
        state_ = State::kStopped;
        Action::Stop(*this);
      }));
}

TimePoint TransportWriteGate::TransportStreamWriteAction::Update(
    TimePoint current_time) {
  return current_time;
}

void TransportWriteGate::TransportStreamWriteAction::Stop() {
  packet_send_action_->Stop();
}

TransportWriteGate::TransportWriteGate(ActionContext action_context,
                                       Ptr<ITransport> transport)
    : transport_{std::move(transport)},
      stream_info_{},
      transport_connection_subscription_{
          transport_->ConnectionSuccess().Subscribe(
              *this, MethodPtr<&TransportWriteGate::GateUpdate>{})},
      transport_disconnection_subscription_{
          transport_->ConnectionError().Subscribe(
              *this, MethodPtr<&TransportWriteGate::GateUpdate>{})},
      transport_read_data_subscription_{transport_->ReceiveEvent().Subscribe(
          *this, MethodPtr<&TransportWriteGate::ReceiveData>{})},
      write_actions_{action_context},
      failed_write_actions_{action_context} {
  auto connection_info = transport_->GetConnectionInfo();
  stream_info_.max_element_size = connection_info.max_packet_size;
  stream_info_.is_linked =
      connection_info.connection_state == ConnectionState::kConnected;
  stream_info_.is_writeble = stream_info_.is_linked;
  stream_info_.is_soft_writable = stream_info_.is_linked;
}

TransportWriteGate::~TransportWriteGate() = default;

ActionView<StreamWriteAction> TransportWriteGate::Write(
    DataBuffer&& buffer, TimePoint current_time) {
  AE_TELED_DEBUG("Write bytes: size: {}\n data: {}", buffer.size(), buffer);

  // TODO: add checks for writeable and soft writable
  if (!stream_info_.is_linked) {
    return failed_write_actions_.Emplace();
  }

  return write_actions_.Emplace(
      transport_->Send(std::move(buffer), current_time));
}

TransportWriteGate::OutDataEvent::Subscriber
TransportWriteGate::out_data_event() {
  return out_data_event_;
}
TransportWriteGate::GateUpdateEvent::Subscriber
TransportWriteGate::gate_update_event() {
  return gate_update_event_;
}

StreamInfo TransportWriteGate::stream_info() const { return stream_info_; }

void TransportWriteGate::GateUpdate() {
  auto connection_info = transport_->GetConnectionInfo();
  stream_info_.max_element_size = connection_info.max_packet_size;
  stream_info_.is_linked =
      connection_info.connection_state == ConnectionState::kConnected;
  stream_info_.is_writeble = stream_info_.is_linked;
  stream_info_.is_soft_writable = stream_info_.is_linked;
  gate_update_event_.Emit();
}

void TransportWriteGate::ReceiveData(DataBuffer const& data,
                                     TimePoint current_time) {
  AE_TELED_DEBUG("Received data from transport\n data:{}\ttime: {:%H:%M:%S}",
                 data, current_time);
  out_data_event_.Emit(data);
}

}  // namespace ae

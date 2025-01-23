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

#include <utility>

#include "mock_transport.h"

#include "aether/transport/actions/channel_connection_action.h"
#include "aether/transport/itransport.h"

namespace ae {

MocTransportPacketSendAction::MocTransportPacketSendAction(
    ActionContext action_context, DataBuffer d, TimePoint st)
    : PacketSendAction{action_context}, data{std::move(d)}, sent_time{st} {}

TimePoint MocTransportPacketSendAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case PacketSendAction::State::kSuccess:
        Action::Result(*this);
        break;
      case PacketSendAction::State::kFailed:
        Action::Error(*this);
        break;
      case PacketSendAction::State::kStopped:
        Action::Stop(*this);
      default:
        break;
    }
  }
  return current_time;
}

void MocTransportPacketSendAction::Stop() {
  state_.Set(PacketSendAction::State::kStopped);
  Action::Trigger();
}

void MocTransportPacketSendAction::SetState(PacketSendAction::State state) {
  state_.Set(state);
}

MockTransport::MockTransport(ActionContext action_context,
                             ConnectionInfo connection_info)
    : action_list_{action_context},
      connection_info_{std::move(connection_info)} {}

void MockTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;
  ConnectAnswer answer = ConnectAnswer::kAllowed;
  connect_called_event_.Emit(answer);
  switch (answer) {
    case ConnectAnswer::kAllowed:
      Connected();
      break;
    case ConnectAnswer::kDenied:
      Disconnected();
      break;
    case ConnectAnswer::kNoAnswer:
      break;
  }
}

ConnectionInfo const& MockTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
MockTransport::ConnectionSuccess() {
  return connection_success_event_;
}
ITransport::ConnectionErrorEvent::Subscriber MockTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber MockTransport::ReceiveEvent() {
  return receive_event_;
}

ActionView<PacketSendAction> MockTransport::Send(DataBuffer data,
                                                 TimePoint current_time) {
  auto action = action_list_.Emplace(std::move(data), current_time);
  send_data_event_.Emit(*action);

  return action;
}

EventSubscriber<void(MocTransportPacketSendAction& action)>
MockTransport::sent_data_event() {
  return send_data_event_;
}

void MockTransport::SendDataBack(DataBuffer data, TimePoint current_time) {
  receive_event_.Emit(data, current_time);
}

EventSubscriber<void(MockTransport::ConnectAnswer& answer)>
MockTransport::connect_call_event() {
  return connect_called_event_;
}

void MockTransport::Connected() {
  connection_info_.connection_state = ConnectionState::kConnected;
  connection_success_event_.Emit();
}
void MockTransport::Disconnected() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

}  // namespace ae

/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
  
ISerialPort::ISerialPort(ActionContext action_context,
                           IPoller::ptr const& poller, SerialInit const& serial_init)
    : action_context_{std::move(action_context)},
      poller_{poller},
      serial_init_{std::move(serial_init)},
      send_queue_manager_{action_context_},
      notify_error_action_{action_context_} {
  AE_TELE_INFO(kSerialTransport);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = false;

  Connect();
}

ISerialPort::~ISerialPort() { Disconnect(); }

ISerialPort::ReadAction::ReadAction(ActionContext action_context,
                                    ISerialPort& serial_port)
    : Action{action_context},
      serial_port_{&serial_port},
      read_buffer_(4096/*transport_->socket_.GetMaxPacketSize()*/) {}
      
UpdateStatus ISerialPort::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    ReadEvent();
  }
  if (error_event_) {
    return UpdateStatus::Error();
  }
  if (stop_event_) {
    return UpdateStatus::Stop();
  }
  return {};
}

void ISerialPort::ReadAction::Read() {
  /* auto lock = std::lock_guard{transport_->socket_mutex_};

  while (true) {
    auto res = transport_->socket_.Receive(
        Span{read_buffer_.data(), read_buffer_.size()});
    if (!res) {
      AE_TELED_ERROR("Socket receiver error");
      error_event_ = true;
      break;
    }
    if (*res == 0) {
      // No more data to receive
      break;
    }
    auto& new_packet = read_buffers_.emplace_back();
    new_packet.reserve(*res);
    std::copy(std::begin(read_buffer_),
              std::begin(read_buffer_) + static_cast<std::ptrdiff_t>(*res),
              std::back_inserter(new_packet));

    read_event_ = true;
  }
  if (read_event_ || error_event_) {
    Action::Trigger();
  }*/
}

void ISerialPort::ReadAction::Stop() {
  /* stop_event_ = true;
  Action::Trigger();*/
}

void ISerialPort::ReadAction::ReadEvent() {
  /* auto lock = std::lock_guard{transport_->socket_mutex_};
  for (auto& packet : read_buffers_) {
    AE_TELE_DEBUG(kUdpTransportReceive, "Receive data size:{}", packet.size());
    transport_->out_data_event_.Emit(packet);
  }
  read_buffers_.clear();*/
}

ISerialPort::SendAction::SendAction(ActionContext action_context,
                                    ISerialPort& serial_port,
                                    DataBuffer&& data_buffer)
    : SocketPacketSendAction{action_context},
      serial_port_{&serial_port},
      data_buffer_{std::move(data_buffer)} {
  /* if (data_buffer_.size() > transport_->socket_.GetMaxPacketSize()) {
    AE_TELED_ERROR("Send message to big");
    state_ = State::kFailed;
  }*/
}

ISerialPort::SendAction::SendAction(SendAction&& other) noexcept
    : SocketPacketSendAction{std::move(other)},
      data_buffer_{std::move(other.data_buffer_)} {}

void ISerialPort::SendAction::Send() {
  /* state_ = State::kInProgress;

  if (!transport_->socket_mutex_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_mutex_, std::adopt_lock};

  assert(data_buffer_.size() <= transport_->socket_.GetMaxPacketSize());
  auto res =
      transport_->socket_.Send(Span{data_buffer_.data(), data_buffer_.size()});
  if (!res) {
    state_ = State::kFailed;
    return;
  }

  if (*res == 0) {
    // Not sent yet
    return;
  }
  if (*res != data_buffer_.size()) {
    AE_TELED_ERROR("Send error, sent size isn't same as packet size");
    state_ = State::kFailed;
    return;
  }
  state_ = State::kDone;*/
}

ActionPtr<StreamWriteAction> ISerialPort::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kSerialTransportSend, "Send data size:{}", in_data.size());
  auto send_packet_action = send_queue_manager_->AddPacket(
      ActionPtr<SendAction>{action_context_, *this, std::move(in_data)});

  send_action_error_subs_.Push(
      send_packet_action->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Send error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }}));

  return send_packet_action;
}
} /* namespace ae */

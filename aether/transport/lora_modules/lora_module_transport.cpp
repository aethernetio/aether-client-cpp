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

#include "aether/transport/lora_modules/lora_module_transport.h"

#if defined LORA_MODULE_TRANSPORT_ENABLED

#  include "aether/mstream.h"
#  include "aether/reflect/reflect.h"
#  include "aether/mstream_buffers.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
LoraModuleTransport::LoraModuleSend::LoraModuleSend(
    ActionContext action_context, LoraModuleTransport& transport,
    DataBuffer data)
    : SocketPacketSendAction{action_context},
      max_packet_size_{std::move(transport.lora_module_driver_->GetMtu())},
      transport_{&transport},
      data_{std::move(data)} {}

LoraModuleTransport::SendLoraAction::SendLoraAction(
    ActionContext action_context, LoraModuleTransport& transport,
    DataBuffer data)
    : LoraModuleSend{action_context, transport, std::move(data)} {}

void LoraModuleTransport::SendLoraAction::Send() {
  if (state_ == State::kInProgress) {
    return;
  }
  state_ = State::kInProgress;

  // split data to chunks and write them to modem
  for (std::size_t sent_offset = 0; sent_offset < data_.size();) {
    auto remain_to_send = data_.size() - sent_offset;
    auto size_to_send =
        remain_to_send > max_packet_size_ ? max_packet_size_ : remain_to_send;

    auto write_action = transport_->lora_module_driver_->WritePacket(
        transport_->connection_,
        DataBuffer{data_.data() + sent_offset,
                   data_.data() + sent_offset + size_to_send});

    if (!write_action) {
      state_ = State::kFailed;
      Action::Trigger();
      return;
    }

    send_subs_.Push(write_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() {
          state_ = State::kDone;
          Action::Trigger();
        }},
        OnError{[this]() {
          state_ = State::kFailed;
          Action::Trigger();
        }},
        OnStop{[this]() {
          state_ = State::kStopped;
          Action::Trigger();
        }},
    }));
    sent_offset += size_to_send;
  }
}

LoraModuleTransport::LoraModuleTransport(ActionContext action_context,
                                         ILoraModuleDriver& lora_module_driver,
                                         Endpoint address)
    : action_context_{action_context},
      lora_module_driver_{&lora_module_driver},
      address_{std::move(address)},
      protocol_{address_.protocol},
      stream_info_{},
      send_action_queue_manager_{action_context_},
      max_packet_size_{lora_module_driver_->GetMtu()} {
  AE_TELE_INFO(kLoraModuleTransport, "Lora module transport created for {}",
               address_);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = (protocol_ == Protocol::kTcp);
  stream_info_.max_element_size = 2 * max_packet_size_;
  stream_info_.rec_element_size = max_packet_size_;

  Connect();
}

LoraModuleTransport::~LoraModuleTransport() { Disconnect(); }

LoraModuleTransport::StreamUpdateEvent::Subscriber
LoraModuleTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo LoraModuleTransport::stream_info() const { return stream_info_; }

LoraModuleTransport::OutDataEvent::Subscriber
LoraModuleTransport::out_data_event() {
  return out_data_event_;
}

void LoraModuleTransport::Restream() {
  AE_TELED_DEBUG("Lora module transport restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

ActionPtr<StreamWriteAction> LoraModuleTransport::Write(DataBuffer&& in_data) {
  Protocol proto{Protocol::kTcp};

  AE_TELE_DEBUG(kLoraModuleTransportSend, "Send data size {}", in_data.size());

  if (protocol_ == Protocol::kTcp || protocol_ == Protocol::kUdp) {
    proto = Protocol::kLora;
  }

  if (proto == Protocol::kLora) {
    auto send_action = send_action_queue_manager_->AddPacket(
        ActionPtr<SendLoraAction>{action_context_, *this, std::move(in_data)});
    send_action_subs_.Push(
        send_action->StatusEvent().Subscribe(OnError{[this]() {
          AE_TELED_ERROR("Send error, disconnect!");
          OnConnectionFailed();
          Disconnect();
        }}));
    return send_action;
  }

  return ActionPtr<FailedStreamWriteAction>{action_context_};
}

void LoraModuleTransport::Connect() {
  ConnectionLoraIndex connection_index{};

  OnConnected(connection_index);
}

void LoraModuleTransport::OnConnected(ConnectionLoraIndex connection_index) {
  connection_ = connection_index;

  read_packet_sub_ = lora_module_driver_->data_event().Subscribe(
      MethodPtr<&LoraModuleTransport::DataReceived>{this});

  stream_info_.is_writable = true;
  stream_info_.link_state = LinkState::kLinked;
  stream_update_event_.Emit();
}

void LoraModuleTransport::OnConnectionFailed() {
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}

void LoraModuleTransport::Disconnect() {
  if (connection_ == kInvalidConnectionLoraIndex) {
    return;
  }

  connection_ = kInvalidConnectionLoraIndex;
}

void LoraModuleTransport::DataReceived(ConnectionLoraIndex connection,
                                       DataBuffer const& data_in) {
  Protocol proto{Protocol::kTcp};

  if (connection_ != connection) {
    return;
  }

  if (protocol_ == Protocol::kTcp || protocol_ == Protocol::kUdp) {
    proto = Protocol::kLora;
  }

  if (proto == Protocol::kLora) {
    DataReceivedLora(data_in);
  }
}

void LoraModuleTransport::DataReceivedLora(DataBuffer const& data_in) {
  data_packet_collector_.AddData(data_in.data(), data_in.size());
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kModemTransportReceive, "Receive data size {}", data.size());
    out_data_event_.Emit(data);
  }
}

}  // namespace ae

#endif

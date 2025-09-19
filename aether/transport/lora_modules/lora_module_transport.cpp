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
static constexpr auto kMaxPacketSize = 200;

LoraModuleTransport::LoraModuleSend::LoraModuleSend(ActionContext action_context,
                                     LoraModuleTransport& transport, DataBuffer data)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)} {}
      
LoraModuleTransport::SendTcpAction::SendTcpAction(ActionContext action_context,
                                             LoraModuleTransport& transport,
                                             DataBuffer data)
    : LoraModuleSend{action_context, transport, std::move(data)}, sent_offset_{} {}
    
void LoraModuleTransport::SendTcpAction::Send() {
  state_ = State::kInProgress;

  auto remain_to_send = data_.size() - sent_offset_;
  auto size_to_send =
      remain_to_send > kMaxPacketSize ? kMaxPacketSize : remain_to_send;

  // FIXME: How to know if packet sent successfully
  transport_->lora_module_driver_->WritePacket(
      transport_->connection_,
      DataBuffer{data_.data() + sent_offset_,
                 data_.data() + sent_offset_ + size_to_send});

  sent_offset_ += size_to_send;
  if (sent_offset_ >= data_.size()) {
    state_ = State::kDone;
  }
  Action::Trigger();
}

LoraModuleTransport::SendUdpAction::SendUdpAction(ActionContext action_context,
                                             LoraModuleTransport& transport,
                                             DataBuffer data)
    : LoraModuleSend{action_context, transport, std::move(data)} {}
    
void LoraModuleTransport::SendUdpAction::Send() {
  state_ = State::kInProgress;

  // FIXME: How to know if packet sent successfully
  transport_->lora_module_driver_->WritePacket(transport_->connection_, data_);
  state_ = State::kDone;

  Action::Trigger();
}

LoraModuleTransport::LoraModuleReadAction::LoraModuleReadAction(ActionContext action_context,
                                                 LoraModuleTransport& transport)
    : Action{action_context}, transport_{&transport} {}

UpdateStatus LoraModuleTransport::LoraModuleReadAction::Update(TimePoint current_time) {
  if (stopped_) {
    return UpdateStatus::Stop();
  }

  Read();
  return UpdateStatus::Delay(current_time + std::chrono::milliseconds{50});
}

void LoraModuleTransport::LoraModuleReadAction::Stop() {
  stopped_ = true;
  Action::Trigger();
}

LoraModuleTransport::ReadTcpAction::ReadTcpAction(ActionContext action_context,
                                             LoraModuleTransport& transport)
    : LoraModuleReadAction{action_context, transport} {}
   
void LoraModuleTransport::ReadTcpAction::Read() {
  auto data_buffer = transport_->lora_module_driver_->ReadPacket(
      transport_->connection_, std::chrono::milliseconds{50});
  if (data_buffer.size() != 0) {
    data_packet_collector_.AddData(data_buffer.data(), data_buffer.size());
  }

  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kLoraModuleTransportReceive, "Receive data size {}", data.size());
    transport_->out_data_event_.Emit(data);
  }
}

LoraModuleTransport::ReadUdpAction::ReadUdpAction(ActionContext action_context,
                                             LoraModuleTransport& transport)
    : LoraModuleReadAction{action_context, transport} {}
   
void LoraModuleTransport::ReadUdpAction::Read() {
  auto data_buffer = transport_->lora_module_driver_->ReadPacket(
      transport_->connection_, std::chrono::milliseconds{50});
  if (data_buffer.size() == 0) {
    return;
  }

  AE_TELE_DEBUG(kLoraModuleTransportReceive, "Receive data size {}",
                data_buffer.size());
  transport_->out_data_event_.Emit(data_buffer);
}

LoraModuleTransport::LoraModuleTransport(ActionContext action_context,
                               ILoraModuleDriver& lora_module_driver,
                               UnifiedAddress address)
    : action_context_{action_context},
      lora_module_driver_{&lora_module_driver},
      address_{std::move(address)},
      protocol_{
          std::visit([](auto const& arg) { return arg.protocol; }, address_)},
      stream_info_{},
      // poll send queue each 50 ms
      send_action_queue_manager_{action_context_,
                                 std::chrono::milliseconds{50}} {
  AE_TELE_INFO(kLoraModuleTransport, "Lora module transport created for {}", address_);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = (protocol_ == Protocol::kTcp);
  stream_info_.max_element_size = 2 * kMaxPacketSize;
  stream_info_.rec_element_size = kMaxPacketSize;

  Connect();
}

LoraModuleTransport::~LoraModuleTransport() { Disconnect(); }

void LoraModuleTransport::Connect() {
  // open network depend on address type
  connection_ = std::visit(
      reflect::OverrideFunc{[&](IpAddressPortProtocol const& address) {
                              return lora_module_driver_->OpenNetwork(
                                  address.protocol, Format("{}", address.ip),
                                  address.port);
                            }
#  if AE_SUPPORT_CLOUD_DNS
                            ,
                            [&](NameAddress const& address) {
                              return lora_module_driver_->OpenNetwork(
                                  address.protocol, address.name, address.port);
                            }
#  endif
      },
      address_);

  // Connection failed
  if (connection_ == kInvalidConnectionLoraIndex) {
    stream_info_.link_state = LinkState::kLinkError;
    stream_update_event_.Emit();
    return;
  }

  // Connection succeeded
  if (protocol_ == Protocol::kTcp) {
    read_action_ = OwnActionPtr<ReadTcpAction>{action_context_, *this};
  } else if (protocol_ == Protocol::kUdp) {
    read_action_ = OwnActionPtr<ReadUdpAction>{action_context_, *this};
  }

  stream_info_.is_writable = true;
  stream_info_.link_state = LinkState::kLinked;
  stream_update_event_.Emit();
}

LoraModuleTransport::StreamUpdateEvent::Subscriber
LoraModuleTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo LoraModuleTransport::stream_info() const { return stream_info_; }

LoraModuleTransport::OutDataEvent::Subscriber LoraModuleTransport::out_data_event() {
  return out_data_event_;
}

ActionPtr<StreamWriteAction> LoraModuleTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kLoraModuleTransportSend, "Send data size {}", in_data.size());

  if (protocol_ == Protocol::kTcp) {
    auto packet_data = std::vector<std::uint8_t>{};
    VectorWriter<PacketSize> vw{packet_data};
    auto os = omstream{vw};
    // copy data with size
    os << in_data;
    auto send_action =
        send_action_queue_manager_->AddPacket(ActionPtr<SendTcpAction>{
            action_context_, *this, std::move(packet_data)});
    send_action_subs_.Push(
        send_action->StatusEvent().Subscribe(OnError{[this]() {
          AE_TELED_ERROR("Send error, disconnect!");
          OnConnectionFailed();
          Disconnect();
        }}));
    return send_action;
  }
  if (protocol_ == Protocol::kUdp) {
    auto send_action = send_action_queue_manager_->AddPacket(
        ActionPtr<SendUdpAction>{action_context_, *this, std::move(in_data)});
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

void LoraModuleTransport::OnConnectionFailed() {
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}

void LoraModuleTransport::Disconnect() {
  if (connection_ == kInvalidConnectionLoraIndex) {
    return;
  }

  lora_module_driver_->CloseNetwork(connection_);
  connection_ = kInvalidConnectionLoraIndex;
}

}  // namespace ae

#endif

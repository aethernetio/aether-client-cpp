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

#include "aether/transport/modems/modem_tcp.h"

#if defined MODEM_TCP_ENABLED
#  include "aether/mstream.h"
#  include "aether/reflect/reflect.h"
#  include "aether/mstream_buffers.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
static constexpr auto kMaxPacketSize = 1024;

ModemTcpTransport::SendAction::SendAction(ActionContext action_context,
                                          ModemTcpTransport& transport,
                                          DataBuffer data)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)},
      sent_offset_{} {}

void ModemTcpTransport::SendAction::Send() {
  state_ = State::kInProgress;

  auto remain_to_send = data_.size() - sent_offset_;
  auto size_to_send =
      remain_to_send > kMaxPacketSize ? kMaxPacketSize : remain_to_send;

  // FIXME: How to know if packet sent successfully
  transport_->modem_driver_->WritePacket(
      transport_->connection_,
      DataBuffer{data_.data() + sent_offset_,
                 data_.data() + sent_offset_ + size_to_send});

  sent_offset_ += size_to_send;
  if (sent_offset_ >= data_.size()) {
    state_ = State::kDone;
  }
  Action::Trigger();
}

ModemTcpTransport::ReadAction::ReadAction(ActionContext action_context,
                                          ModemTcpTransport& transport)
    : Action{action_context}, transport_{&transport} {}

UpdateStatus ModemTcpTransport::ReadAction::Update(TimePoint current_time) {
  if (stopped_) {
    return UpdateStatus::Stop();
  }

  Read();
  return UpdateStatus::Delay(current_time + std::chrono::milliseconds{50});
}

void ModemTcpTransport::ReadAction::Stop() {
  stopped_ = true;
  Action::Trigger();
}

void ModemTcpTransport::ReadAction::Read() {
  auto data_buffer = transport_->modem_driver_->ReadPacket(
      transport_->connection_, std::chrono::milliseconds{50});
  if (data_buffer.size() != 0) {
    data_packet_collector_.AddData(data_buffer.data(), data_buffer.size());
  }

  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kModemTcpTransportReceive, "Receive data size {}",
                  data.size());
    transport_->out_data_event_.Emit(data);
  }
}

ModemTcpTransport::ModemTcpTransport(ActionContext action_context,
                                     IModemDriver& modem_driver,
                                     UnifiedAddress address)
    : action_context_{action_context},
      modem_driver_{&modem_driver},
      address_{std::move(address)},
      stream_info_{},
      // poll send queue each 50 ms
      send_action_queue_manager_{action_context_,
                                 std::chrono::milliseconds{50}} {
  AE_TELE_INFO(kModemTcpTransport, "Modem tcp transport created for {}",
               address_);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  stream_info_.max_element_size = 2 * kMaxPacketSize;
  stream_info_.rec_element_size = kMaxPacketSize;
}

ModemTcpTransport::~ModemTcpTransport() { Disconnect(); }

void ModemTcpTransport::Connect() {
  // open network depend on address type
  connection_ = std::visit(
      reflect::OverrideFunc{[&](IpAddressPortProtocol const& address) {
                              return modem_driver_->OpenNetwork(
                                  address.protocol, Format("{}", address.ip),
                                  address.port);
                            }
#  if AE_SUPPORT_CLOUD_DNS
                            ,
                            [&](NameAddress const& address) {
                              return modem_driver_->OpenNetwork(
                                  address.protocol, address.name, address.port);
                            }
#  endif
      },
      address_);

  // Connection failed
  if (connection_ == kInvalidConnectionIndex) {
    stream_info_.link_state = LinkState::kLinkError;
    stream_update_event_.Emit();
    return;
  }

  // Connection succeeded
  read_action_ = OwnActionPtr<ReadAction>{action_context_, *this};

  stream_info_.link_state = LinkState::kLinked;
  stream_update_event_.Emit();
}

ModemTcpTransport::StreamUpdateEvent::Subscriber
ModemTcpTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo ModemTcpTransport::stream_info() const { return stream_info_; }

ModemTcpTransport::OutDataEvent::Subscriber
ModemTcpTransport::out_data_event() {
  return out_data_event_;
}

ActionPtr<StreamWriteAction> ModemTcpTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kModemTcpTransportSend, "Send data size {}", in_data.size());

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << in_data;

  auto send_action = send_action_queue_manager_->AddPacket(
      ActionPtr<SendAction>{action_context_, *this, std::move(packet_data)});
  send_action_subs_.Push(send_action->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELED_ERROR("Send error, disconnect!");
    OnConnectionFailed();
    Disconnect();
  }}));
  return send_action;
}

void ModemTcpTransport::OnConnectionFailed() {
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}

void ModemTcpTransport::Disconnect() {
  if (connection_ == kInvalidConnectionIndex) {
    return;
  }

  modem_driver_->CloseNetwork(connection_);
  connection_ = kInvalidConnectionIndex;
}
}  // namespace ae

#endif

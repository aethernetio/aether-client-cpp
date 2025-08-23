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
#  include "aether/reflect/reflect.h"
#  include "aether/mstream.h"
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
  state_ = State::kProgress;

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
    state_ = State::kSuccess;
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
    transport_->data_receive_event_.Emit(data, Now());
  }
}

ModemTcpTransport::ModemTcpTransport(ActionContext action_context,
                                     IModemDriver& modem_driver,
                                     UnifiedAddress address)
    : action_context_{action_context},
      modem_driver_{&modem_driver},
      address_{std::move(address)},
      connection_info_{kMaxPacketSize, ConnectionState::kUndefined,
                       ConnectionType::kConnectionOriented,
                       Reliability::kUnreliable},
      // poll send queue each 50 ms
      send_action_queue_manager_{action_context_,
                                 std::chrono::milliseconds{50}} {
  AE_TELE_INFO(kModemTcpTransport, "Modem tcp transport created for {}",
               address_);
}

ModemTcpTransport::~ModemTcpTransport() { Disconnect(); }

void ModemTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;
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
    connection_info_.connection_state = ConnectionState::kDisconnected;
    connection_error_event_.Emit();
    return;
  }

  // Connection succeeded
  read_action_ = OwnActionPtr<ReadAction>{action_context_, *this};

  connection_info_.connection_state = ConnectionState::kConnected;
  connection_success_event_.Emit();
}

ConnectionInfo const& ModemTcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ModemTcpTransport::ConnectionSuccessEvent::Subscriber
ModemTcpTransport::ConnectionSuccess() {
  return EventSubscriber{connection_success_event_};
}

ModemTcpTransport::ConnectionErrorEvent::Subscriber
ModemTcpTransport::ConnectionError() {
  return EventSubscriber{connection_error_event_};
}

ModemTcpTransport::DataReceiveEvent::Subscriber
ModemTcpTransport::ReceiveEvent() {
  return EventSubscriber{data_receive_event_};
}

ActionPtr<PacketSendAction> ModemTcpTransport::Send(DataBuffer data,
                                                    TimePoint current_time) {
  AE_TELE_DEBUG(kModemTcpTransportSend, "Send data size {} at {:%H:%M:%S}",
                data.size(), current_time);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

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
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
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

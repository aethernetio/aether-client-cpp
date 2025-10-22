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

#include "aether/transport/modems/modem_transport.h"

#if MODEM_TRANSPORT_ENABLED

#  include "aether/mstream.h"
#  include "aether/reflect/reflect.h"
#  include "aether/mstream_buffers.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
static constexpr auto kMaxPacketSize = 1024;

ModemTransport::ModemSend::ModemSend(ActionContext action_context,
                                     ModemTransport& transport, DataBuffer data)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)} {}

ModemTransport::SendTcpAction::SendTcpAction(ActionContext action_context,
                                             ModemTransport& transport,
                                             DataBuffer data)
    : ModemSend{action_context, transport, std::move(data)} {}

void ModemTransport::SendTcpAction::Send() {
  if (state_ == State::kInProgress) {
    return;
  }
  state_ = State::kInProgress;

  // split data to chunks and write them to modem
  for (std::size_t sent_offset = 0; sent_offset < data_.size();) {
    auto remain_to_send = data_.size() - sent_offset;
    auto size_to_send =
        remain_to_send > kMaxPacketSize ? kMaxPacketSize : remain_to_send;

    auto write_action = transport_->modem_driver_->WritePacket(
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

ModemTransport::SendUdpAction::SendUdpAction(ActionContext action_context,
                                             ModemTransport& transport,
                                             DataBuffer data)
    : ModemSend{action_context, transport, std::move(data)} {}

void ModemTransport::SendUdpAction::Send() {
  if (state_ == State::kInProgress) {
    return;
  }
  state_ = State::kInProgress;

  auto write_action =
      transport_->modem_driver_->WritePacket(transport_->connection_, data_);

  if (!write_action) {
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }

  send_sub_ = write_action->StatusEvent().Subscribe(ActionHandler{
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
  });
}

ModemTransport::ModemTransport(ActionContext action_context,
                               IModemDriver& modem_driver,
                               UnifiedAddress address)
    : action_context_{action_context},
      modem_driver_{&modem_driver},
      address_{std::move(address)},
      protocol_{
          std::visit([](auto const& arg) { return arg.protocol; }, address_)},
      stream_info_{},
      send_action_queue_manager_{action_context_} {
  AE_TELE_INFO(kModemTransport, "Modem transport created for {}", address_);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = (protocol_ == Protocol::kTcp);
  stream_info_.max_element_size = 2 * kMaxPacketSize;
  stream_info_.rec_element_size = kMaxPacketSize;

  Connect();
}

ModemTransport::~ModemTransport() { Disconnect(); }

ModemTransport::StreamUpdateEvent::Subscriber
ModemTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo ModemTransport::stream_info() const { return stream_info_; }

ModemTransport::OutDataEvent::Subscriber ModemTransport::out_data_event() {
  return out_data_event_;
}

void ModemTransport::Restream() {
  AE_TELED_DEBUG("Modem transport restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

ActionPtr<StreamWriteAction> ModemTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kModemTransportSend, "Send data size {}", in_data.size());

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
        }}));
    return send_action;
  }

  return ActionPtr<FailedStreamWriteAction>{action_context_};
}

void ModemTransport::Connect() {
  // open network depend on address type
  auto connection_operation = std::visit(
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

  if (!connection_operation) {
    OnConnectionFailed();
    return;
  }

  connection_sub_ = connection_operation->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this](auto const& action) { OnConnected(action.value()); }},
      OnError{[this]() { OnConnectionFailed(); }},
  });
}

void ModemTransport::OnConnected(ConnectionIndex connection_index) {
  connection_ = connection_index;

  read_packet_sub_ = modem_driver_->data_event().Subscribe(
      *this, MethodPtr<&ModemTransport::DataReceived>{});

  stream_info_.is_writable = true;
  stream_info_.link_state = LinkState::kLinked;
  stream_update_event_.Emit();
}

void ModemTransport::OnConnectionFailed() {
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

void ModemTransport::Disconnect() {
  if (connection_ == kInvalidConnectionIndex) {
    return;
  }

  modem_driver_->CloseNetwork(connection_);
  connection_ = kInvalidConnectionIndex;

  stream_update_event_.Emit();
}

void ModemTransport::DataReceived(ConnectionIndex connection,
                                  DataBuffer const& data_in) {
  if (connection_ != connection) {
    return;
  }
  if (protocol_ == Protocol::kTcp) {
    DataReceivedTcp(data_in);
  } else if (protocol_ == Protocol::kUdp) {
    DataReceivedUdp(data_in);
  }
}

void ModemTransport::DataReceivedTcp(DataBuffer const& data_in) {
  data_packet_collector_.AddData(data_in.data(), data_in.size());
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kModemTransportReceive, "Receive data size {}", data.size());
    out_data_event_.Emit(data);
  }
}

void ModemTransport::DataReceivedUdp(DataBuffer const& data_in) {
  out_data_event_.Emit(data_in);
}

}  // namespace ae

#endif

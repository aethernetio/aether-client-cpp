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
#  include "aether/mstream_buffers.h"
#  include "aether/write_action/failed_write_action.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
static constexpr auto kMaxPacketSize = 1024;

ModemTransport::ModemSend::ModemSend(AeContext const& ae_context,
                                     ModemTransport& transport, DataBuffer data)
    : ae_context_{ae_context},
      alive_ctx_{ae_context_, this},
      transport_{&transport},
      data_{std::move(data)},
      is_done_{false},
      in_progress_{false} {}

bool ModemTransport::ModemSend::is_done() const { return is_done_; }
bool ModemTransport::ModemSend::re_enqueue() const { return false; }

void ModemTransport::ModemSend::SetStatus(Status status) noexcept {
  is_done_ = true;
  ae_context_.scheduler().Task([imalive{alive_ctx_.View()}, status]() {
    if (imalive) {
      imalive->WriteAction::SetStatus(status);
    }
  });
}

ModemTransport::SendTcpAction::SendTcpAction(AeContext const& ae_context,
                                             ModemTransport& transport,
                                             DataBuffer data)
    : ModemSend{ae_context, transport, std::move(data)}, packets_on_the_go_{} {}

void ModemTransport::SendTcpAction::Send() {
  if (in_progress_) {
    return;
  }
  in_progress_ = true;

  // split data to chunks and write them to modem
  auto data_span = std::span<std::uint8_t const>{data_.data(), data_.size()};
  while (!data_span.empty()) {
    auto remain_to_send = data_span.size();
    auto size_to_send =
        remain_to_send > kMaxPacketSize ? kMaxPacketSize : remain_to_send;

    auto chunk = data_span.subspan(0, size_to_send);
    data_span = data_span.subspan(size_to_send);
    auto* write_op =
        transport_->modem_driver_->WritePacket(transport_->connection_, chunk);
    if (write_op == nullptr) {
      AE_TELED_ERROR("Modem tcp write op error");
      SetStatus(Status::kFail);
      return;
    }

    packets_on_the_go_++;
    send_subs_ += write_op->result_event().Subscribe([this](auto const& res) {
      if (res) {
        AE_TELED_DEBUG("Modem tcp {} bytes written", res.value());
        if ((--packets_on_the_go_) == 0) {
          SetStatus(Status::kSuccess);
        }
      } else {
        AE_TELED_ERROR("Modem tcp write failed with error {}",
                       static_cast<int>(res.error()));
        SetStatus(Status::kFail);
      }
    });
  }
}

ModemTransport::SendUdpAction::SendUdpAction(AeContext const& ae_context,
                                             ModemTransport& transport,
                                             DataBuffer data)
    : ModemSend{ae_context, transport, std::move(data)} {}

void ModemTransport::SendUdpAction::Send() {
  if (in_progress_) {
    return;
  }
  in_progress_ = true;

  auto* write_op =
      transport_->modem_driver_->WritePacket(transport_->connection_, data_);
  if (write_op == nullptr) {
    AE_TELED_ERROR("Modem udp write op error");
    SetStatus(Status::kFail);
    return;
  }
  send_sub_ = write_op->result_event().Subscribe([&](auto const& res) {
    if (res) {
      AE_TELED_DEBUG("Modem udp {} bytes written", res.value());
      SetStatus(Status::kSuccess);
    } else {
      AE_TELED_ERROR("Modem udp write failed with error {}",
                     static_cast<int>(res.error()));
      SetStatus(Status::kFail);
    }
  });
}

ModemTransport::ModemTransport(AeContext const& ae_context,
                               IModemDriver& modem_driver, Endpoint address)
    : ae_context_{ae_context},
      modem_driver_{&modem_driver},
      address_{std::move(address)},
      protocol_{address_.protocol},
      stream_info_{},
      packet_queue_manager_{MakePacketQueueManager(ae_context_, address_)} {
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

WriteAction& ModemTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kModemTransportSend, "Send data size {}", in_data.size());

  if (protocol_ == Protocol::kTcp) {
    return WriteTcp(std::move(in_data));
  }
  if (protocol_ == Protocol::kUdp) {
    return WriteUdp(std::move(in_data));
  }

  return FailedWrite();
}

WriteAction& ModemTransport::WriteTcp(DataBuffer&& in_data) {
  auto& send_queue_manager =
      std::get<PacketQueueManager<SendTcpAction, kTcpSendQueueSize>>(
          packet_queue_manager_);

  // Make TCP packet with its size at the beginning
  auto packet_data = std::vector<std::uint8_t>{};
  packet_data.reserve(in_data.size() + 4);
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << std::move(in_data);  // NOLINT

  auto* send_action = send_queue_manager.AddPacket(
      SendTcpAction{ae_context_, *this, std::move(packet_data)});
  if (send_action == nullptr) {
    return FailedWrite();
  }

  send_action_subs_ +=
      send_action->status_event().Subscribe([this](auto status) {
        if (status == WriteAction::Status::kFail) {
          AE_TELED_ERROR("Send error, disconnect!");
          OnConnectionFailed();
        }
      });
  return *send_action;
}
WriteAction& ModemTransport::WriteUdp(DataBuffer&& in_data) {
  auto& send_queue_manager =
      std::get<PacketQueueManager<SendUdpAction, kTcpSendQueueSize>>(
          packet_queue_manager_);

  auto* send_action = send_queue_manager.AddPacket(
      SendUdpAction{ae_context_, *this, std::move(in_data)});
  if (send_action == nullptr) {
    return FailedWrite();
  }

  send_action_subs_ +=
      send_action->status_event().Subscribe([this](auto status) {
        if (status == WriteAction::Status::kFail) {
          AE_TELED_ERROR("Send error, disconnect!");
          OnConnectionFailed();
        }
      });
  return *send_action;
}

WriteAction& ModemTransport::FailedWrite() {
  if (!failed_write_ || failed_write_->is_finished()) {
    failed_write_.emplace(ae_context_);
  }
  return *failed_write_;
}

void ModemTransport::Connect() {
  // open network depend on address type
  auto* connection_op = modem_driver_->OpenNetwork(
      address_.protocol, Format("{}", address_.address), address_.port);

  if (connection_op == nullptr) {
    OnConnectionFailed();
    return;
  }

  connection_sub_ =
      connection_op->result_event().Subscribe([this](auto const& res) {
        if (res) {
          OnConnected(res.value());
        } else {
          OnConnectionFailed();
        }
      });
}

void ModemTransport::OnConnected(ConnectionIndex connection_index) {
  connection_ = connection_index;

  read_packet_sub_ = modem_driver_->data_event().Subscribe(
      MethodPtr<&ModemTransport::DataReceived>{this});

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

ModemTransport::PacketQueueManagerVar ModemTransport::MakePacketQueueManager(
    AeContext const& ae_context, Endpoint const& endpoint) {
  switch (endpoint.protocol) {
    case Protocol::kTcp: {
      return PacketQueueManagerVar{
          std::in_place_type_t<
              PacketQueueManager<SendTcpAction, kTcpSendQueueSize>>{},
          ae_context};
    }
    default: {
      return PacketQueueManagerVar{
          std::in_place_type_t<
              PacketQueueManager<SendUdpAction, kTcpSendQueueSize>>{},
          ae_context};
    }
  }
}

}  // namespace ae

#endif

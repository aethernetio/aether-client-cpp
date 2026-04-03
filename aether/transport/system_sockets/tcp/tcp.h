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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_

#include "aether/config.h"
#if AE_SUPPORT_TCP

#  if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
      defined(__FreeBSD__) || defined(ESP_PLATFORM) || defined(_WIN32)

#    define SYSTEM_SOCKET_TCP_TRANSPORT_ENABLED 1

#    include <mutex>
#    include <optional>

#    include "aether/common.h"
#    include "aether/ae_context.h"
#    include "aether/poller/poller.h"
#    include "aether/events/multi_subscription.h"

#    include "aether/mstream.h"
#    include "aether/mstream_buffers.h"
#    include "aether/stream_api/istream.h"
#    include "aether/transport/packet_send_action.h"
#    include "aether/transport/packet_queue_manager.h"
#    include "aether/transport/data_packet_collector.h"
#    include "aether/write_action/failed_write_action.h"
#    include "aether/transport/system_sockets/sockets/isocket.h"

#    include "aether/transport/transport_tele.h"

namespace ae {
namespace tcp_internal {
template <typename Sock, typename Lock>
class SendAction final : public PacketSendAction {
 public:
  SendAction(AeContext const& ae_context, Sock& socket, Lock& lock,
             DataBuffer&& data_buffer)
      : ae_context_{ae_context},
        socket_{&socket},
        lock_{&lock},
        data_{std::move(data_buffer)} {}

  AE_CLASS_MOVE_ONLY(SendAction)

  void Send() override {
    reenqueue_ = false;
    if (!lock_->try_lock()) {
      reenqueue_ = true;
      return;
    }
    auto sl = std::scoped_lock{std::adopt_lock, *lock_};

    auto size_to_send = data_.size() - sent_offset_;
    auto res = socket_->Send(Span{data_.data() + sent_offset_, size_to_send});
    if (!res) {
      AE_TELED_ERROR("Data has not been written");
      SetStatus(WriteAction::Status::kFail);
      return;
    }
    AE_TELED_DEBUG("Data has been written size {} data {}", *res, data_);
    sent_offset_ += *res;

    if (sent_offset_ >= data_.size()) {
      SetStatus(WriteAction::Status::kSuccess);
    }
    // reenque must happen on event from socket
  }

  void Stop() noexcept override {
    is_done_ = true;
    WriteAction::SetStatus(WriteAction::Status::kStop);
  }

  bool is_done() const override { return is_done_; }
  bool re_enqueue() const override { return reenqueue_; }

 protected:
  void SetStatus(WriteAction::Status status) noexcept override {
    is_done_ = true;
    ae_context_.scheduler().Task(
        [&, status]() { WriteAction::SetStatus(status); });
  }

 private:
  AeContext ae_context_;
  Sock* socket_;
  Lock* lock_;
  DataBuffer data_;
  std::size_t sent_offset_ = 0;
  bool is_done_ = false;
  bool reenqueue_ = false;
};

class TcpBase : public ByteIStream {
 public:
  TcpBase(AeContext const& ae_context, AddressPort endpoint) noexcept;

  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 protected:
  void OnConnection(ISocket::ConnectionState connection_state);
  void OnRecvData(Span<std::uint8_t> data);
  void OnReadyToWrite();
  void OnSocketError();
  virtual void Disconnect() = 0;
  FailedWriteAction& FailedWrite();

  AeContext ae_context_;
  AddressPort endpoint_;
  std::mutex lock_;
  MultiSubscription send_action_subs_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  StreamDataPacketCollector data_packet_collector_;
  std::atomic_bool read_event_{false};
  std::optional<FailedWriteAction> failed_write_;
};
}  // namespace tcp_internal

static constexpr std::size_t kTcpSendQueueSize = 10;

template <typename Socket>
class TcpTransport final : public tcp_internal::TcpBase {
 public:
  using SendAction = tcp_internal::SendAction<Socket, std::mutex>;

  TcpTransport(AeContext const& ae_context, Ptr<IPoller> const& poller,
               AddressPort endpoint)
      : tcp_internal::TcpBase{ae_context, std::move(endpoint)},
        socket_{poller},
        queue_manager_{ae_context_}  // read_action_{action_context_, *this}
  {
    AE_TELE_INFO(kTcpTransport);
    AE_TELE_INFO(kTcpTransportConnect, "Tcp connect to endpoint {}", endpoint_);
    // Make connection
    socket_.RecvData(MethodPtr<&TcpTransport::OnRecvData>{this})
        .ReadyToWrite(MethodPtr<&TcpTransport::OnReadyToWrite>{this})
        .Error(MethodPtr<&TcpTransport::OnSocketError>{this})
        .Connect(endpoint_, MethodPtr<&TcpTransport::OnConnection>{this});

    stream_info_.link_state = LinkState::kUnlinked;
    stream_info_.is_reliable = true;
    // TODO: find a better value for max element size
    stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();
  }

  ~TcpTransport() override {
    stream_info_.link_state = LinkState::kUnlinked;
    AE_TELED_ERROR("Remove tcp transport!");
    Disconnect();
  }

  WriteAction& Write(DataBuffer&& in_data) override {
    AE_TELE_DEBUG(kTcpTransportSend, "Socket {} send data size {}", endpoint_,
                  in_data.size());

    auto packet_data = std::vector<std::uint8_t>{};
    VectorWriter<PacketSize> vw{packet_data};
    auto os = omstream{vw};
    // copy data with size
    os << std::move(in_data);  // NOLINT

    auto* send_action = queue_manager_.AddPacket(
        SendAction{ae_context_, socket_, lock_, std::move(packet_data)});
    if (send_action == nullptr) {
      return FailedWrite();
    }

    send_action_subs_ +=
        send_action->status_event().Subscribe([this](auto status) {
          if (status == WriteAction::Status::kFail) {
            AE_TELED_ERROR("Send error, disconnect!");
            stream_info_.link_state = LinkState::kLinkError;
            Disconnect();
          }
        });
    return *send_action;
  }

 protected:
  void Disconnect() override {
    AE_TELE_INFO(kTcpTransportDisconnect, "Disconnect from {}", endpoint_);
    stream_info_.is_writable = false;
    {
      auto lock = std::scoped_lock{lock_};
      socket_.Disconnect();
    }
    stream_update_event_.Emit();
  }

 private:
  void OnReadyToWrite() { queue_manager_.Send(); }

  Socket socket_;
  PacketQueueManager<SendAction, kTcpSendQueueSize> queue_manager_;
};

}  // namespace ae

#  endif
#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_

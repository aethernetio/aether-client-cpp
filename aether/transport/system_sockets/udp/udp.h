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
#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_H_

#include "aether/config.h"

#if AE_SUPPORT_UDP

#  if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
      defined(__FreeBSD__) || defined(ESP_PLATFORM) || defined(_WIN32)

#    define SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED 1

#    include <mutex>
#    include <optional>

#    include "aether/ae_context.h"
#    include "aether/events/multi_subscription.h"

#    include "aether/poller/poller.h"
#    include "aether/stream_api/istream.h"
#    include "aether/transport/transport_tele.h"
#    include "aether/transport/packet_send_action.h"
#    include "aether/transport/packet_queue_manager.h"
#    include "aether/write_action/failed_write_action.h"
#    include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
namespace upd_internal {
template <typename Socket, typename Lock>
class SendAction final : public PacketSendAction {
 public:
  SendAction(AeContext const& ae_context, Socket& socket, Lock& lock,
             DataBuffer&& data_buffer)
      : ae_context_{ae_context},
        socket_{&socket},
        lock_{&lock},
        data_{std::move(data_buffer)} {}

  AE_CLASS_MOVE_ONLY(SendAction)

  void Send() override {
    if (!lock_->try_lock()) {
      reenque_ = true;
      return;
    }
    auto sl = std::scoped_lock{std::adopt_lock, *lock_};

    auto res = socket_->Send(Span{data_.data(), data_.size()});
    if (!res) {
      AE_TELED_ERROR("Data has not been written");
      SetStatus(WriteAction::Status::kFail);
      return;
    }
    AE_TELED_DEBUG("Data has been written size {}", data_.size());

    if (*res == 0) {
      // Not sent yet
      return;
    }
    if (*res != data_.size()) {
      AE_TELED_ERROR("Send error, sent size isn't same as packet size");
      SetStatus(WriteAction::Status::kFail);
      return;
    }
    SetStatus(WriteAction::Status::kSuccess);
  }

  void Stop() noexcept override {
    is_done_ = true;
    WriteAction::SetStatus(WriteAction::Status::kStop);
  }

  bool is_done() const override { return is_done_; }
  bool re_enqueue() const override { return reenque_; }

 protected:
  void SetStatus(WriteAction::Status status) noexcept override {
    is_done_ = true;
    ae_context_.scheduler().Task(
        [&, status]() { WriteAction::SetStatus(status); });
  }

 private:
  AeContext ae_context_;
  Socket* socket_;
  Lock* lock_;
  DataBuffer data_;
  bool reenque_ = false;
  bool is_done_ = false;
};

class UdpBase : public ByteIStream {
 public:
  UdpBase(AeContext const& ae_context, AddressPort endpoint);

  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 protected:
  void OnConnected(ISocket::ConnectionState connection_state);
  void OnRecvData(Span<std::uint8_t> data);
  void OnSocketError();
  virtual void Disconnect() = 0;
  FailedWriteAction& FailedWrite();

  AeContext ae_context_;
  AddressPort endpoint_;

  std::mutex socket_mutex_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::vector<DataBuffer> read_buffers_;
  std::atomic_bool read_event_{false};
  std::optional<FailedWriteAction> failed_write_;
};

}  // namespace upd_internal

template <typename Socket>
class UdpTransport final : public upd_internal::UdpBase {
 public:
  using SendAction = upd_internal::SendAction<Socket, std::mutex>;

  UdpTransport(AeContext const& ae_context, Ptr<IPoller> const& poller,
               AddressPort endpoint)
      : upd_internal::UdpBase{ae_context, std::move(endpoint)},
        socket_{poller},
        send_queue_manager_{ae_context} {
    // Make connection
    AE_TELE_INFO(kUdpTransportConnect, "Udp connect to endpoint {}", endpoint_);

    socket_.RecvData(MethodPtr<&UdpTransport::OnRecvData>{this})
        .ReadyToWrite(MethodPtr<&UdpTransport::OnReadyToWrite>{this})
        .Error(MethodPtr<&UdpTransport::OnSocketError>{this})
        .Connect(endpoint_, MethodPtr<&UdpTransport::OnConnected>{this});
  }

  ~UdpTransport() override {
    stream_info_.link_state = LinkState::kUnlinked;
    Disconnect();
  }

  WriteAction& Write(DataBuffer&& in_data) override {
    AE_TELE_DEBUG(kUdpTransportSend, "Socket {} send data size:{}", endpoint_,
                  in_data.size());
    auto* send_action = send_queue_manager_.AddPacket(
        SendAction{ae_context_, socket_, socket_mutex_, std::move(in_data)});
    if (send_action == nullptr) {
      return FailedWrite();
    }

    send_action_error_subs_ +=
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
  void OnReadyToWrite() { send_queue_manager_.Send(); }

  void Disconnect() override {
    AE_TELE_INFO(kUdpTransportDisconnect, "Disconnect from {}", endpoint_);
    stream_info_.is_writable = false;
    {
      auto lock = std::scoped_lock{socket_mutex_};
      socket_.Disconnect();
    }
    stream_update_event_.Emit();
  }

 private:
  Socket socket_;
  PacketQueueManager<SendAction> send_queue_manager_;
  MultiSubscription send_action_error_subs_;
};
}  // namespace ae

#  endif
#endif

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_H_

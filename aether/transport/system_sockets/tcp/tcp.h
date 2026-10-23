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
#    include <vector>

#    include "aether/ae_context.h"
#    include "aether/common.h"
#    include "aether/events/events.h"
#    include "aether/poller/poller.h"
#    include "aether/vector_buffer.h"

#    include "aether/stream_api/istream.h"
#    include "aether/transport/data_packet_collector.h"
#    include "aether/transport/packet_queue_manager.h"
#    include "aether/transport/packet_send_action.h"
#    include "aether/transport/system_sockets/sockets/isocket.h"
#    include "aether/write_action/failed_write_action.h"

#    include "aether/transport/transport_tele.h"

namespace ae {
namespace tcp_internal {
template <typename Sock>
class SendAction final : public PacketSendAction {
 public:
  SendAction(AeContext const& ae_context, Sock& socket,
             DataBuffer&& data_buffer)
      : PacketSendAction{ae_context},
        ae_context_{ae_context},
        socket_{&socket},
        data_{std::move(data_buffer)} {}

  AE_CLASS_MOVE_ONLY(SendAction)

  /**
   * \brief Merge another fully formed, not-yet-sent frame into this action.
   * \return false if either action already started sending or other is not a
   * single-frame action.
   */
  bool Absorb(SendAction&& other) noexcept {
    if (sent_offset_ != 0 || other.sent_offset_ != 0) {
      return false;
    }
    if (is_done_ || other.is_done_) {
      return false;
    }
    if (socket_ != other.socket_) {
      return false;
    }
    if (!other.segments_.empty() || !other.absorbed_.empty()) {
      return false;
    }
    if (other.data_.empty()) {
      return false;
    }

    EnsurePrimarySegment();
    data_.insert(data_.end(), other.data_.begin(), other.data_.end());
    other.data_.clear();
    auto const absorbed_index = absorbed_.size();
    absorbed_.push_back(std::move(other));
    segments_.push_back({data_.size(), false, absorbed_index});
    return true;
  }

  void Send() override {
    reenqueue_ = false;

    auto size_to_send = data_.size() - sent_offset_;
    auto res = socket_->Send(Span{data_.data() + sent_offset_, size_to_send});
    if (!res) {
      AE_TELED_ERROR("Data has not been written");
      AbortPending(WriteAction::Status::kFail);
      return;
    }
    if (*res == 0) {
      return;
    }

    AE_TELED_DEBUG("Data has been written size {} data {}", *res, data_);
    sent_offset_ += *res;
    OnSentProgress();
    // reenque must happen on event from socket
  }

  void Stop() noexcept override {
    if (segments_.empty()) {
      is_done_ = true;
      set_status_.Reset();
      WriteAction::SetStatus(WriteAction::Status::kStop);
      return;
    }
    AbortPending(WriteAction::Status::kStop);
  }

  bool is_done() const override { return is_done_; }
  bool re_enqueue() const override { return reenqueue_; }

 protected:
  void SetStatus(WriteAction::Status status) noexcept override {
    FinalizeHost(status);
  }

 private:
  struct Segment {
    std::size_t end;
    bool is_host;
    std::size_t absorbed_index;
  };

  SendAction& SegmentAction(Segment const& segment) noexcept {
    return segment.is_host ? *this : absorbed_[segment.absorbed_index];
  }

  void EnsurePrimarySegment() {
    if (!segments_.empty()) {
      return;
    }
    segments_.push_back({data_.size(), true, 0});
  }

  bool HostHasMultipleSegments() const noexcept {
    return segments_.size() > 1;
  }

  void EmitWriteStatus(SendAction& target,
                       WriteAction::Status status) noexcept {
    target.set_status_.Reset();
    target.set_status_ = target.ae_context_.scheduler().Task(
        [&target, status]() { target.WriteAction::SetStatus(status); });
  }

  void CompleteSegment(SendAction& target,
                       WriteAction::Status status) noexcept {
    if (&target != this) {
      target.is_done_ = true;
      EmitWriteStatus(target, status);
      return;
    }
    if (HostHasMultipleSegments()) {
      EmitWriteStatus(target, status);
      return;
    }
    FinalizeHost(status);
  }

  void FinalizeHost(WriteAction::Status status) noexcept {
    is_done_ = true;
    EmitWriteStatus(*this, status);
  }

  void OnSentProgress() noexcept {
    if (segments_.empty()) {
      if (sent_offset_ >= data_.size()) {
        FinalizeHost(WriteAction::Status::kSuccess);
      }
      return;
    }

    while (completed_segment_idx_ < segments_.size() &&
           sent_offset_ >= segments_[completed_segment_idx_].end) {
      CompleteSegment(SegmentAction(segments_[completed_segment_idx_]),
                      WriteAction::Status::kSuccess);
      ++completed_segment_idx_;
    }
    if (sent_offset_ >= data_.size()) {
      is_done_ = true;
    }
  }

  void AbortPending(WriteAction::Status status) noexcept {
    if (segments_.empty()) {
      FinalizeHost(status);
      return;
    }
    is_done_ = true;
    for (; completed_segment_idx_ < segments_.size();
         ++completed_segment_idx_) {
      CompleteSegment(SegmentAction(segments_[completed_segment_idx_]), status);
    }
  }

  AeContext ae_context_;
  Sock* socket_;
  DataBuffer data_;
  std::vector<Segment> segments_;
  std::vector<SendAction> absorbed_;
  std::size_t sent_offset_ = 0;
  std::size_t completed_segment_idx_ = 0;
  bool is_done_ = false;
  bool reenqueue_ = false;
  TaskSubscription set_status_;
};

class TcpBase : public ByteIStream {
 public:
  TcpBase(AeContext const& ae_context, AddressPort endpoint) noexcept;

  StreamUpdateEvent const& stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent const& out_data_event() override;
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
  MultiSubscription send_action_subs_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  std::mutex buffer_lock_;
  StreamDataPacketCollector data_packet_collector_;
  std::atomic_bool read_event_{false};
  TaskSubscription read_event_sub_;
  TaskSubscription conn_state_sub_;
  std::optional<FailedWriteAction> failed_write_;
};
}  // namespace tcp_internal

template <typename Socket, std::size_t QueueSize>
class TcpTransport final : public tcp_internal::TcpBase {
 public:
  using SendAction = tcp_internal::SendAction<Socket>;

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
  }

  ~TcpTransport() override {
    stream_info_.link_state = LinkState::kUnlinked;
    AE_TELED_ERROR("Remove tcp transport!");
    Disconnect();
  }

  WriteAction& Write(DataBuffer&& in_data) override {
    assert(in_data.size() != 0);

    AE_TELE_DEBUG(kTcpTransportSend, "Socket {} send data size {}", endpoint_,
                  in_data.size());

    auto packet_data = std::vector<std::uint8_t>{};
    VectorBuffer<PacketSize> vw{packet_data};
    vw.Write(seri::SizeWriteTag(in_data.size()));
    vw.Write(seri::DataWriteTag{std::move(in_data).data(),
                                std::move(in_data).size()});

    auto* send_action =
        queue_manager_.AddPacket(ae_context_, socket_, std::move(packet_data));
    if (send_action == nullptr) {
      AE_TELED_ERROR("Queue manager is full");
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
    socket_.Disconnect();
    stream_update_event_.Emit();
  }

 private:
  void OnReadyToWrite() { queue_manager_.Send(); }

  Socket socket_;
  PacketQueueManager<SendAction, QueueSize> queue_manager_;
};

}  // namespace ae

#  endif
#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_

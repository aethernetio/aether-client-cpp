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

#    define COMMON_TCP_TRANSPORT_ENABLED 1

#    include <mutex>

#    include "aether/common.h"
#    include "aether/ptr/ptr_view.h"
#    include "aether/poller/poller.h"
#    include "aether/actions/action.h"
#    include "aether/actions/notify_action.h"
#    include "aether/actions/action_context.h"
#    include "aether/events/multi_subscription.h"

#    include "aether/stream_api/istream.h"
#    include "aether/transport/data_packet_collector.h"
#    include "aether/transport/socket_packet_send_action.h"
#    include "aether/transport/socket_packet_queue_manager.h"

#    include "aether/transport/system_sockets/sockets/tcp_sockets_factory.h"

namespace ae {
class TcpTransport final : public ByteIStream {
  static constexpr int kInvalidSocket = -1;

  class ConnectionAction : public Action<ConnectionAction> {
   public:
    enum class State : std::uint8_t {
      kConnecting,
      kWaitConnection,
      kGetConnectionUpdate,
      kConnectionFailed,
      kConnected,
      kStopped,
    };

    ConnectionAction(ActionContext action_context, TcpTransport& transport);

    AE_CLASS_NO_COPY_MOVE(ConnectionAction)

    UpdateStatus Update();
    void Stop();

   private:
    void Connect();
    void WaitConnection();
    void ConnectionUpdate();

    TcpTransport* transport_;
    StateMachine<State> state_;
    Subscription state_changed_subscription_;
    IPoller::OnPollEventSubscriber::Subscription poller_subscription_;
  };

  using SocketEventAction = NotifyAction;

  class SendAction : public SocketPacketSendAction {
   public:
    SendAction(ActionContext action_context, TcpTransport& transport,
               DataBuffer data);

    AE_CLASS_NO_COPY_MOVE(SendAction)

    void Send() override;

   private:
    TcpTransport* transport_;
    DataBuffer data_;
    std::size_t sent_offset_ = 0;
    Subscription state_changed_subscription_;
  };

  class ReadAction : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, TcpTransport& transport);

    AE_CLASS_NO_COPY_MOVE(ReadAction)

    UpdateStatus Update();
    void Read();
    void Stop();

   private:
    void DataReceived();

    TcpTransport* transport_;
    StreamDataPacketCollector data_packet_collector_;
    DataBuffer read_buffer_;
    std::atomic_bool read_event_{};
    std::atomic_bool error_event_{};
    std::atomic_bool stop_event_{};
  };

 public:
  TcpTransport(ActionContext action_context, IPoller::ptr const& poller,
               IpAddressPort const& endpoint);
  ~TcpTransport() override;

  ActionPtr<StreamWriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  void OnConnected();

  void ReadSocket();
  void WriteSocket();
  void ErrorSocket();

  void Disconnect();

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  IpAddressPort endpoint_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  TcpSocket socket_;
  std::mutex socket_lock_;

  OwnActionPtr<SocketPacketQueueManager<SendAction>> send_queue_manager_;
  OwnActionPtr<ConnectionAction> connection_action_;
  OwnActionPtr<ReadAction> read_action_;
  OwnActionPtr<SocketEventAction> socket_error_action_;

  Subscription connection_sub_;
  MultiSubscription send_action_subs_;
  Subscription read_action_error_sub_;
  IPoller::OnPollEventSubscriber::Subscription socket_poll_subscription_;
  Subscription socket_error_subscription_;
};

}  // namespace ae

#  endif
#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_

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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_TCP_POSIX_TCP_H_
#define AETHER_TRANSPORT_LOW_LEVEL_TCP_POSIX_TCP_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(ESP_PLATFORM)

#  define POSIX_TCP_TRANSPORT_ENABLED 1

#  include <mutex>

#  include "aether/common.h"
#  include "aether/poller/poller.h"
#  include "aether/actions/action.h"
#  include "aether/actions/notify_action.h"
#  include "aether/actions/action_context.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/transport/itransport.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/transport/low_level/tcp/socket_packet_send_action.h"
#  include "aether/transport/low_level/tcp/socket_packet_queue_manager.h"

#  include "aether/transport/low_level/sockets/tcp_sockets_factory.h"

namespace ae {
class PosixTcpTransport : public ITransport {
  static constexpr int kInvalidSocket = -1;

  class ConnectionAction : public Action<ConnectionAction> {
   public:
    enum class State : std::uint8_t {
      kConnecting,
      kWaitConnection,
      kGetConnectionUpdate,
      kConnectionFailed,
      kConnected,
    };

    ConnectionAction(ActionContext action_context,
                     PosixTcpTransport& transport);

    ActionResult Update();

   private:
    void Connect();
    void WaitConnection();
    void ConnectionUpdate();

    PosixTcpTransport* transport_;
    StateMachine<State> state_;
    Subscription state_changed_subscription_;
    IPoller::OnPollEventSubscriber::Subscription poller_subscription_;
  };

  using SocketEventAction = NotifyAction<>;

  class UnixPacketSendAction : public SocketPacketSendAction {
   public:
    UnixPacketSendAction(ActionContext action_context,
                         PosixTcpTransport& transport, DataBuffer data,
                         TimePoint current_time);

    UnixPacketSendAction(UnixPacketSendAction&& other) noexcept;

    void Send() override;

   private:
    PosixTcpTransport* transport_;
    DataBuffer data_;
    TimePoint current_time_;
    std::size_t sent_offset_ = 0;
    Subscription state_changed_subscription_;
  };

  class UnixPacketReadAction : public Action<UnixPacketReadAction> {
   public:
    UnixPacketReadAction(ActionContext action_context,
                         PosixTcpTransport& transport);

    ActionResult Update();
    void Read();

   private:
    void DataReceived();

    PosixTcpTransport* transport_;
    StreamDataPacketCollector data_packet_collector_;
    DataBuffer read_buffer_;
    std::atomic_bool read_event_{};
    std::atomic_bool error_{};
  };

 public:
  PosixTcpTransport(ActionContext action_context, IPoller::ptr poller,
                    IpAddressPort const& endpoint);
  ~PosixTcpTransport() override;

  void Connect() override;
  ConnectionInfo const& GetConnectionInfo() const override;
  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;
  ConnectionErrorEvent::Subscriber ConnectionError() override;

  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionView<PacketSendAction> Send(DataBuffer data,
                                    TimePoint current_time) override;

 private:
  void OnConnected();
  void OnConnectionFailed();

  void ReadSocket();
  void WriteSocket();
  void ErrorSocket();

  void Disconnect();

  ActionContext action_context_;
  IPoller::ptr poller_;
  IpAddressPort endpoint_;

  ConnectionInfo connection_info_;
  DataReceiveEvent data_receive_event_;
  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;

  TcpSocket socket_;
  std::mutex socket_lock_;

  SocketPacketQueueManager<UnixPacketSendAction> socket_packet_queue_manager_;
  ActionOpt<ConnectionAction> connection_action_;
  ActionOpt<UnixPacketReadAction> read_action_;
  SocketEventAction socket_error_action_;

  Subscription connection_error_sub_;
  MultiSubscription send_action_subs_;
  Subscription read_action_error_sub_;
  IPoller::OnPollEventSubscriber::Subscription socket_poll_subscription_;
  Subscription socket_error_subscription_;
};

}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_POSIX_TCP_H_

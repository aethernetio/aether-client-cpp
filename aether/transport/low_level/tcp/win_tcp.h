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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_TCP_WIN_TCP_H_
#define AETHER_TRANSPORT_LOW_LEVEL_TCP_WIN_TCP_H_

#if defined _WIN32
#  define WIN_TCP_TRANSPORT_ENABLED 1

#  include <mutex>
#  include <memory>
#  include <atomic>
#  include <optional>

#  include "aether/state_machine.h"
#  include "aether/events/events.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/poller/poller.h"
#  include "aether/actions/notify_action.h"
#  include "aether/actions/action_context.h"

#  include "aether/poller/win_poller.h"  // for WinPollerOverlapped

#  include "aether/socket_initializer.h"
#  include "aether/transport/itransport.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/transport/low_level/tcp/socket_packet_send_action.h"
#  include "aether/transport/low_level/tcp/socket_packet_queue_manager.h"

namespace ae {
constexpr auto InvalidSocketValue = static_cast<DescriptorType::Socket>(~0);

class WinTcpTransport : public ITransport {
  class ConnectionAction : public Action<ConnectionAction> {
   public:
    enum class State {
      kConnecting,
      kWaitConnection,
      kConnectionUpdate,
      kConnectionFailed,
      kConnected,
    };

    ConnectionAction(ActionContext action_context, WinTcpTransport& transport);
    ~ConnectionAction() override;

    TimePoint Update(TimePoint current_time) override;

   private:
    void ConnectionUpdate();
    void Connect();

    WinTcpTransport* transport_;
    DescriptorType::Socket socket_ = InvalidSocketValue;
    StateMachine<State> state_;
    Subscription state_changed_subscription_;
    std::shared_ptr<std::mutex> alive_checker_;
  };

  using SocketEventAction = NotifyAction<>;

  class WinTcpPacketSendAction : public SocketPacketSendAction {
   public:
    WinTcpPacketSendAction(ActionContext action_context,
                           WinTcpTransport& transport, DataBuffer data,
                           TimePoint current_time);

    WinTcpPacketSendAction(WinTcpPacketSendAction&& other) noexcept;

    void Send() override;

   private:
    void MakeSend();
    void OnSend();

    WinTcpTransport* transport_;
    DataBuffer send_buffer_;
    TimePoint current_time_;

    std::size_t send_offset_;
    Subscription send_event_subscription_;
    Subscription state_changed_subscription_;
    std::atomic_bool send_pending_;
  };

  class WinTcpReadAction : public Action<WinTcpReadAction> {
   public:
    WinTcpReadAction(ActionContext action_context, WinTcpTransport& transport);

    TimePoint Update(TimePoint current_time) override;

    void RecvUpdate();
    void HandleReceivedData();
    void OnRecv();

   private:
    WinTcpTransport* transport_;
    DataBuffer recv_tmp_buffer_;
    StreamDataPacketCollector data_packet_collector_;
    std::atomic_bool read_event_;
    std::atomic_bool error_event_;
  };

 public:
  WinTcpTransport(ActionContext action_context, IPoller::ptr poller,
                  IpAddressPort const& endpoint);
  ~WinTcpTransport() override;

  void Connect() override;
  ConnectionInfo const& GetConnectionInfo() const override;
  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;
  ConnectionErrorEvent::Subscriber ConnectionError() override;

  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionView<PacketSendAction> Send(DataBuffer data,
                                    TimePoint current_time) override;

 private:
  void OnConnect(DescriptorType::Socket socket);
  void OnConnectionError();
  void OnSocketError();

  void Disconnect();

  ActionContext action_context_;
  IPoller::ptr poller_;
  IpAddressPort endpoint_;

  SocketInitializer socket_initializer_;

  ConnectionInfo connection_info_;

  DescriptorType::Socket socket_;
  std::mutex socket_lock_;

  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;
  DataReceiveEvent data_receive_event_;

  std::optional<ConnectionAction> connection_action_;
  std::optional<WinTcpReadAction> read_action_;
  SocketPacketQueueManager<WinTcpPacketSendAction> socket_packet_queue_manager_;

  SocketEventAction socket_error_action_;

  Event<void()> send_event_;

  MultiSubscription connection_subs_;
  MultiSubscription send_action_subs_;
  Subscription socket_poll_sub_;
  Subscription socket_error_sub_;
  Subscription read_error_sub_;

  // READ / WRITE operation states
  WinPollerOverlapped read_overlapped_;
  WinPollerOverlapped write_overlapped_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_WIN_TCP_H_

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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_TCP_LWIP_TCP_H_
#define AETHER_TRANSPORT_LOW_LEVEL_TCP_LWIP_TCP_H_

#if (defined(ESP_PLATFORM))

#  define LWIP_TCP_TRANSPORT_ENABLED 1

#  include <optional>

#  include "aether/common.h"
#  include "aether/poller/poller.h"

#  include "aether/events/events.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/actions/action_context.h"

#  include "aether/transport/itransport.h"
#  include "aether/transport/low_level/tcp/data_packet_collector.h"
#  include "aether/transport/low_level/tcp/socket_packet_send_action.h"
#  include "aether/transport/low_level/tcp/socket_packet_queue_manager.h"

namespace ae {
class LwipTcpTransport : public ITransport {
  static constexpr int kInvalidSocket = -1;
  static constexpr int RCV_TIMEOUT_SEC = 0;
  static constexpr int RCV_TIMEOUT_USEC = 10000;
  static constexpr int LWIP_NETIF_MTU = 1500;

  class ConnectionAction : public Action<ConnectionAction> {
   public:
    enum class State {
      kConnecting,
      kNotConnected,
      kConnected,
    };

    explicit ConnectionAction(ActionContext action_context,
                              LwipTcpTransport& transport);

    TimePoint Update(TimePoint current_time) override;
    int get_socket() const;
    State get_state() const;

   private:
    void Connect();

    IpAddressPort endpoint_;
    int socket_ = kInvalidSocket;
    StateMachine<State> state_;
    Subscription state_changed_subscription_;
  };

  class SocketEventAction : public NotifyAction<SocketEventAction> {
   public:
    using NotifyAction::NotifyAction;
  };

  class LwipTcpPacketSendAction : public SocketPacketSendAction {
   public:
    LwipTcpPacketSendAction(ActionContext action_context, int socket,
                            DataBuffer data, TimePoint current_time);

    void Send() override;

   private:
    int socket_;
    DataBuffer data_;
    TimePoint current_time_;
    std::size_t sent_offset_ = 0;
    Subscription state_changed_subscription_;
  };

 public:
  LwipTcpTransport(ActionContext action_context, IPoller::ptr poller,
                   IpAddressPort const& endpoint);
  ~LwipTcpTransport() override;

  void Connect() override;
  ConnectionInfo const& GetConnectionInfo() const override;
  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;
  ConnectionErrorEvent::Subscriber ConnectionError() override;

  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionView<PacketSendAction> Send(DataBuffer data,
                                    TimePoint current_time) override;

 private:
  void OnConnected(int socket);
  void OnConnectionFailed();

  void OnSocketUpdate(TimePoint current_time);

  void ReadSocket(TimePoint current_time);
  void WriteSocket();

  void OnDataReceived(TimePoint current_time);

  void Disconnect();

  ActionContext action_context_;
  IPoller::ptr poller_;
  IpAddressPort endpoint_;

  ConnectionInfo connection_info_;
  DataReceiveEvent data_receive_event_;
  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;

  int socket_ = kInvalidSocket;

  SocketPacketQueueManager<LwipTcpPacketSendAction>
      socket_packet_queue_manager_;
  StreamDataPacketCollector data_packet_collector_;

  std::optional<ConnectionAction> connection_action_;
  SocketEventAction socket_event_action_;

  MultiSubscription connection_action_subscriptions_;
  Subscription socket_poll_subscription_;
  Subscription socket_event_subscription_;
};

}  // namespace ae
#endif

#endif  // AETHER_TRANSPORT_LOW_LEVEL_TCP_LWIP_TCP_H_

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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LTE_TCP_SOCKET_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LTE_TCP_SOCKET_H_

#define LTE_TCP_TRANSPORT_ENABLED 1

#include "aether/common.h"
#include "aether/poller/poller.h"

#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"

#include "aether/actions/notify_action.h"
#include "aether/actions/action_context.h"

#include "aether/transport/itransport.h"
#include "aether/transport/low_level/tcp/data_packet_collector.h"
#include "aether/transport/low_level/tcp/socket_packet_send_action.h"
#include "aether/transport/low_level/tcp/socket_packet_queue_manager.h"

namespace ae {
 
class LteTcpTransport : public ITransport {
 
   class LteTcpPacketSendAction : public SocketPacketSendAction {
   public:
    LteTcpPacketSendAction(ActionContext action_context,
                           LteTcpTransport& transport, DataBuffer data,
                           TimePoint current_time);

    LteTcpPacketSendAction(LteTcpPacketSendAction&& other) noexcept;

    void Send() override;

   private:
    LteTcpTransport* transport_;
    DataBuffer data_;
    TimePoint current_time_;
    std::size_t sent_offset_ = 0;
    Subscription state_changed_subscription_;
  };
  
public:
  LteTcpTransport(ActionContext action_context, IPoller::ptr poller,
                   IpAddressPort const& endpoint);
  ~LteTcpTransport() override;

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

  void Disconnect();
  
  ActionContext action_context_;
  IPoller::ptr poller_;
  IpAddressPort endpoint_;

  ConnectionInfo connection_info_;
  DataReceiveEvent data_receive_event_;
  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;
  
  SocketPacketQueueManager<LteTcpPacketSendAction>
      socket_packet_queue_manager_;
};

}  // namespace ae

#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LTE_TCP_SOCKET_H_
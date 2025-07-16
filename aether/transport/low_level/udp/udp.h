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
#ifndef AETHER_TRANSPORT_LOW_LEVEL_UDP_UDP_H_
#define AETHER_TRANSPORT_LOW_LEVEL_UDP_UDP_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(ESP_PLATFORM) || defined(_WIN32)

#  define COMMON_UDP_TRANSPORT_ENABLED 1

#  include <mutex>

#  include "aether/ptr/ptr_view.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_list.h"
#  include "aether/actions/notify_action.h"
#  include "aether/actions/action_context.h"
#  include "aether/events/event_subscription.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/poller/poller.h"
#  include "aether/transport/itransport.h"
#  include "aether/transport/low_level/socket_packet_send_action.h"
#  include "aether/transport/low_level/socket_packet_queue_manager.h"
#  include "aether/transport/low_level/sockets/udp_sockets_factory.h"

namespace ae {
class UdpTransport : public ITransport {
  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, UdpTransport& transport);

    ActionResult Update();

    void Read();

   private:
    void ReadEvent();

    UdpTransport* transport_;
    DataBuffer read_buffer_;
    std::vector<DataBuffer> read_buffers_;
    std::atomic_bool read_event_{false};
    std::atomic_bool error_event_{false};
  };

  class SendAction final : public SocketPacketSendAction {
   public:
    SendAction(ActionContext action_context, UdpTransport& transport,
               DataBuffer&& data_buffer);
    SendAction(SendAction&& other) noexcept;

    void Send() override;

   private:
    UdpTransport* transport_;
    DataBuffer data_buffer_;
  };

  using ErrorEventAction = NotifyAction<>;

 public:
  UdpTransport(ActionContext action_context, IPoller::ptr const& poller,
               IpAddressPort endpoint);
  ~UdpTransport() override;

  void Connect() override;
  ConnectionInfo const& GetConnectionInfo() const override;
  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;
  ConnectionErrorEvent::Subscriber ConnectionError() override;
  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionView<PacketSendAction> Send(DataBuffer data,
                                    TimePoint current_time) override;

 private:
  void OnRead();
  void OnWrite();
  void OnError();

  void OnConnectionError();
  void Disconnect();

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  IpAddressPort endpoint_;

  std::mutex socket_mutex_;
  UdpSocket socket_;

  ConnectionInfo connection_info_;
  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;
  DataReceiveEvent data_receive_event_;
  ErrorEventAction notify_error_action_;
  SocketPacketQueueManager<SendAction> send_queue_manager_;
  ActionOpt<ReadAction> read_action_;

  IPoller::OnPollEventSubscriber::Subscription socket_event_sub_;
  Subscription socket_error_sub_;
  Subscription read_error_sub_;
  MultiSubscription send_action_error_subs_;
};
}  // namespace ae

#endif

#endif  // AETHER_TRANSPORT_LOW_LEVEL_UDP_UDP_H_

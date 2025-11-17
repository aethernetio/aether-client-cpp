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

#    include "aether/ptr/ptr_view.h"
#    include "aether/actions/action.h"
#    include "aether/actions/action_ptr.h"
#    include "aether/actions/notify_action.h"
#    include "aether/actions/action_context.h"
#    include "aether/events/event_subscription.h"
#    include "aether/events/multi_subscription.h"

#    include "aether/poller/poller.h"
#    include "aether/stream_api/istream.h"
#    include "aether/transport/socket_packet_send_action.h"
#    include "aether/transport/socket_packet_queue_manager.h"
#    include "aether/transport/system_sockets/sockets/udp_sockets_factory.h"

namespace ae {
class UdpTransport : public ByteIStream {
  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, UdpTransport& transport);

    UpdateStatus Update();

    void Read();
    void Stop();

   private:
    void ReadEvent();

    UdpTransport* transport_;
    DataBuffer read_buffer_;
    std::vector<DataBuffer> read_buffers_;
    std::atomic_bool read_event_{false};
    std::atomic_bool error_event_{false};
    std::atomic_bool stop_event_{false};
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

  using ErrorEventAction = NotifyAction;

 public:
  UdpTransport(ActionContext action_context, IPoller::ptr const& poller,
               IpAddressPort endpoint);
  ~UdpTransport() override;

  ActionPtr<StreamWriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  void Connect();
  void ReadSocket();
  void WriteSocket();
  void ErrorSocket();

  void Disconnect();

  ActionContext action_context_;
  PtrView<IPoller> poller_;
  IpAddressPort endpoint_;

  std::mutex socket_mutex_;
  UdpSocket socket_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  OwnActionPtr<SocketPacketQueueManager<SendAction>> send_queue_manager_;
  OwnActionPtr<ErrorEventAction> notify_error_action_;
  OwnActionPtr<ReadAction> read_action_;

  Subscription socket_event_sub_;
  Subscription socket_error_sub_;
  Subscription read_error_sub_;
  MultiSubscription send_action_error_subs_;
};
}  // namespace ae

#  endif
#endif

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_H_

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
#    include "aether/poller/poller.h"
#    include "aether/actions/action.h"
#    include "aether/actions/notify_action.h"
#    include "aether/actions/action_context.h"
#    include "aether/events/multi_subscription.h"

#    include "aether/stream_api/istream.h"
#    include "aether/transport/data_packet_collector.h"
#    include "aether/transport/socket_packet_send_action.h"
#    include "aether/transport/socket_packet_queue_manager.h"
#    include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
class TcpTransport final : public ByteIStream {
  using SocketEventAction = NotifyAction;
  using SocketConnectAction = NotifyAction;

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
  };

  class ReadAction : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, TcpTransport& transport);

    AE_CLASS_NO_COPY_MOVE(ReadAction)

    UpdateStatus Update();
    void Read(Span<std::uint8_t> buffer);
    void Stop();

   private:
    void DataReceived();

    TcpTransport* transport_;
    StreamDataPacketCollector data_packet_collector_;
    std::atomic_bool read_event_{};
    std::atomic_bool stop_event_{};
  };

 public:
  TcpTransport(ActionContext action_context, Ptr<IPoller> const& poller,
               AddressPort endpoint);
  ~TcpTransport() override;

  ActionPtr<WriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  void OnConnection(ISocket::ConnectionState connection_state);
  void OnRecvData(Span<std::uint8_t> data);
  void OnReadyToWrite();
  void OnSocketError();

  void Disconnect();

  ActionContext action_context_;
  AddressPort endpoint_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  std::unique_ptr<ISocket> socket_;
  std::mutex socket_lock_;

  OwnActionPtr<SocketPacketQueueManager<SendAction>> send_queue_manager_;
  OwnActionPtr<ReadAction> read_action_;
  OwnActionPtr<SocketConnectAction> socket_connect_action_;
  OwnActionPtr<SocketEventAction> socket_error_action_;

  MultiSubscription send_action_subs_;
  Subscription socket_connect_sub_;
  Subscription socket_error_sub_;
};

}  // namespace ae

#  endif
#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_TCP_TCP_H_

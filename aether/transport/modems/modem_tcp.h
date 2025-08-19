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

#ifndef AETHER_TRANSPORT_MODEMS_MODEM_TCP_H_
#define AETHER_TRANSPORT_MODEMS_MODEM_TCP_H_

#include "aether/config.h"
#include "aether/modems/modem_factory.h"

#if defined AE_MODEM_ENABLED and AE_SUPPORT_TCP
#  define MODEM_TCP_ENABLED 1
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/events/event_subscription.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/modems/imodem_driver.h"
#  include "aether/transport/itransport.h"
#  include "aether/transport/data_packet_collector.h"
#  include "aether/transport/socket_packet_send_action.h"
#  include "aether/transport/socket_packet_queue_manager.h"

namespace ae {
class ModemTcpTransport final : public ITransport {
  class SendAction final : public SocketPacketSendAction {
   public:
    SendAction(ActionContext action_context, ModemTcpTransport& transport,
               DataBuffer data);

    void Send() override;

   private:
    ModemTcpTransport* transport_;
    DataBuffer data_;
    std::size_t sent_offset_;
  };

  class ReadAction final : public Action<ReadAction> {
   public:
    ReadAction(ActionContext action_context, ModemTcpTransport& transport);

    UpdateStatus Update(TimePoint current_time);

    void Stop();

   private:
    void Read();

    ModemTcpTransport* transport_;
    StreamDataPacketCollector data_packet_collector_;
    DataBuffer data_buffer_;
    bool stopped_ = false;
  };

 public:
  ModemTcpTransport(ActionContext action_context, IModemDriver& modem_driver,
                    UnifiedAddress address);
  ~ModemTcpTransport() override;

  void Connect() override;

  ConnectionInfo const& GetConnectionInfo() const override;

  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;

  ConnectionErrorEvent::Subscriber ConnectionError() override;

  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionPtr<PacketSendAction> Send(DataBuffer data,
                                   TimePoint current_time) override;

 private:
  void OnConnectionFailed();
  void Disconnect();

  ActionContext action_context_;
  IModemDriver* modem_driver_;
  UnifiedAddress address_;

  ConnectionInfo connection_info_;

  ConnectionIndex connection_ = kInvalidConnectionIndex;

  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;
  DataReceiveEvent data_receive_event_;

  OwnActionPtr<SocketPacketQueueManager<SendAction>> send_action_queue_manager_;
  OwnActionPtr<ReadAction> read_action_;

  MultiSubscription send_action_subs_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_MODEMS_MODEM_TCP_H_

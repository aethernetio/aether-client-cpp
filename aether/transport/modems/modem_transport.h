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

#ifndef AETHER_TRANSPORT_MODEMS_MODEM_TRANSPORT_H_
#define AETHER_TRANSPORT_MODEMS_MODEM_TRANSPORT_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  define MODEM_TRANSPORT_ENABLED 1

#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/stream_api/istream.h"
#  include "aether/modems/imodem_driver.h"
#  include "aether/transport/data_packet_collector.h"
#  include "aether/transport/socket_packet_send_action.h"
#  include "aether/transport/socket_packet_queue_manager.h"

namespace ae {
class ModemTransport final : public ByteIStream {
  class ModemSend : public SocketPacketSendAction {
   public:
    ModemSend(ActionContext action_context, ModemTransport& transport,
              DataBuffer data);

   protected:
    ModemTransport* transport_;
    DataBuffer data_;
  };

  class SendTcpAction final : public ModemSend {
   public:
    SendTcpAction(ActionContext action_context, ModemTransport& transport,
                  DataBuffer data);
    void Send() override;

   private:
    void SendPacket(DataBuffer const& data);
    MultiSubscription send_subs_;
  };

  class SendUdpAction final : public ModemSend {
   public:
    SendUdpAction(ActionContext action_context, ModemTransport& transport,
                  DataBuffer data);

    void Send() override;

   private:
    Subscription send_sub_;
  };

 public:
  ModemTransport(ActionContext action_context, IModemDriver& modem_driver,
                 Endpoint address);
  ~ModemTransport() override;

  ActionPtr<WriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  void Connect();
  void OnConnected(ConnectionIndex connection_index);
  void OnConnectionFailed();
  void Disconnect();

  void DataReceived(ConnectionIndex connection, DataBuffer const& data_in);
  void DataReceivedTcp(DataBuffer const& data_in);
  void DataReceivedUdp(DataBuffer const& data_in);

  ActionContext action_context_;
  IModemDriver* modem_driver_;
  Endpoint address_;
  Protocol protocol_;

  StreamInfo stream_info_;

  ConnectionIndex connection_ = kInvalidConnectionIndex;
  OwnActionPtr<SocketPacketQueueManager<ModemSend>> send_action_queue_manager_;
  StreamDataPacketCollector data_packet_collector_;

  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  MultiSubscription send_action_subs_;
  Subscription connection_sub_;
  Subscription read_packet_sub_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_MODEMS_MODEM_TRANSPORT_H_

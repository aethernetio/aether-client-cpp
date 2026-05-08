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
#  include <variant>
#  include <optional>

#  include "aether/ae_context.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/stream_api/istream.h"
#  include "aether/modems/imodem_driver.h"
#  include "aether/transport/packet_send_action.h"
#  include "aether/transport/packet_queue_manager.h"
#  include "aether/transport/data_packet_collector.h"
#  include "aether/write_action/failed_write_action.h"

namespace ae {

// TODO: add config
static constexpr std::size_t kTcpSendQueueSize = 10;

class ModemTransport final : public ByteIStream {
  class ModemSend : public PacketSendAction {
   public:
    ModemSend(AeContext const& ae_context, ModemTransport& transport,
              DataBuffer data);

    bool is_done() const override;
    bool re_enqueue() const override;

   protected:
    void SetStatus(Status status) noexcept override;

    AeContext ae_context_;
    TaskSubscription task_sub_;
    ModemTransport* transport_;
    DataBuffer data_;
    bool is_done_;
    bool in_progress_;
  };

  class SendTcpAction final : public ModemSend {
   public:
    SendTcpAction(AeContext const& ae_context, ModemTransport& transport,
                  DataBuffer data);
    void Send() override;

   private:
    void SendPacket(DataBuffer const& data);
    MultiSubscription send_subs_;
    std::size_t packets_on_the_go_;
  };

  class SendUdpAction final : public ModemSend {
   public:
    SendUdpAction(AeContext const& ae_context, ModemTransport& transport,
                  DataBuffer data);

    void Send() override;

   private:
    Subscription send_sub_;
  };

  using PacketQueueManagerVar =
      std::variant<PacketQueueManager<SendTcpAction, kTcpSendQueueSize>,
                   PacketQueueManager<SendUdpAction, kTcpSendQueueSize>>;

 public:
  ModemTransport(AeContext const& ae_context, IModemDriver& modem_driver,
                 Endpoint address);
  ~ModemTransport() override;

  WriteAction& Write(DataBuffer&& in_data) override;
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

  WriteAction& WriteTcp(DataBuffer&& in_data);
  WriteAction& WriteUdp(DataBuffer&& in_data);
  WriteAction& FailedWrite();

  static PacketQueueManagerVar MakePacketQueueManager(
      AeContext const& ae_context, Endpoint const& endpoint);

  AeContext ae_context_;
  IModemDriver* modem_driver_;
  Endpoint address_;
  Protocol protocol_;

  StreamInfo stream_info_;

  ConnectionIndex connection_ = kInvalidConnectionIndex;

  PacketQueueManagerVar packet_queue_manager_;
  std::optional<FailedWriteAction> failed_write_;

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

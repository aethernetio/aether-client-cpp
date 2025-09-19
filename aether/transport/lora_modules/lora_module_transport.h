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

#ifndef AETHER_TRANSPORT_LORA_MODULES_LORA_MODULE_TRANSPORT_H_
#define AETHER_TRANSPORT_LORA_MODULES_LORA_MODULE_TRANSPORT_H_

#include "aether/config.h"
#include "aether/lora_modules/lora_module_factory.h"

#if (AE_LORA_MODULE_ENABLED == 1) && ((AE_SUPPORT_TCP == 1) || (AE_SUPPORT_UDP == 1))
#  define LORA_MODULE_TRANSPORT_ENABLED 1
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/events/multi_subscription.h"

#  include "aether/stream_api/istream.h"
#  include "aether/lora_modules/ilora_module_driver.h"
#  include "aether/transport/data_packet_collector.h"
#  include "aether/transport/lora_modules/send_queue_poller.h"
#  include "aether/transport/socket_packet_send_action.h"

namespace ae {
class LoraModuleTransport final : public ByteIStream {
  class LoraModuleSend : public SocketPacketSendAction {
   public:
    LoraModuleSend(ActionContext action_context, LoraModuleTransport& transport,
              DataBuffer data);

   protected:
    LoraModuleTransport* transport_;
    DataBuffer data_;
  };

  class SendTcpAction final : public LoraModuleSend {
   public:
    SendTcpAction(ActionContext action_context, LoraModuleTransport& transport,
                  DataBuffer data);

    void Send() override;

   private:
    std::size_t sent_offset_;
  };

  class SendUdpAction final : public LoraModuleSend {
   public:
    SendUdpAction(ActionContext action_context, LoraModuleTransport& transport,
                  DataBuffer data);

    void Send() override;
  };

  class LoraModuleReadAction : public Action<LoraModuleReadAction> {
   public:
    LoraModuleReadAction(ActionContext action_context, LoraModuleTransport& transport);

    UpdateStatus Update(TimePoint current_time);
    void Stop();

    virtual void Read() = 0;

   protected:
    LoraModuleTransport* transport_;
    bool stopped_ = false;
  };

  class ReadTcpAction final : public LoraModuleReadAction {
   public:
    ReadTcpAction(ActionContext action_context, LoraModuleTransport& transport);

    void Read() override;

   private:
    StreamDataPacketCollector data_packet_collector_;
  };

  class ReadUdpAction final : public LoraModuleReadAction {
   public:
    ReadUdpAction(ActionContext action_context, LoraModuleTransport& transport);

    void Read() override;
  };

 public:
  LoraModuleTransport(ActionContext action_context, ILoraModuleDriver& lora_module_driver,
                 UnifiedAddress address);
  ~LoraModuleTransport() override;

  ActionPtr<StreamWriteAction> Write(DataBuffer&& in_data) override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;

 private:
  void Connect();
  void OnConnectionFailed();
  void Disconnect();

  ActionContext action_context_;
  ILoraModuleDriver* lora_module_driver_;
  UnifiedAddress address_;
  Protocol protocol_;

  StreamInfo stream_info_;

  ConnectionLoraIndex connection_ = kInvalidConnectionLoraIndex;

  OwnActionPtr<SendQueuePoller<LoraModuleSend>> send_action_queue_manager_;
  OwnActionPtr<LoraModuleReadAction> read_action_;

  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  MultiSubscription send_action_subs_;
};
}  // namespace ae

#endif

#endif  // AETHER_TRANSPORT_LORA_MODULES_LORA_MODULE_TRANSPORT_H_
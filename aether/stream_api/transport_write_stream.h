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

#ifndef AETHER_STREAM_API_TRANSPORT_WRITE_STREAM_H_
#define AETHER_STREAM_API_TRANSPORT_WRITE_STREAM_H_

#include "aether/common.h"
#include "aether/types/data_buffer.h"
#include "aether/actions/action_ptr.h"
#include "aether/transport/itransport.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"
#include "aether/events/event_subscription.h"

#include "aether/stream_api/istream.h"

namespace ae {
class TransportWriteStream final : public ByteIStream {
  class TransportStreamWriteAction final : public StreamWriteAction {
   public:
    explicit TransportStreamWriteAction(
        ActionContext action_context,
        ActionPtr<PacketSendAction> packet_send_action);

    void Stop() override;

   private:
    ActionPtr<PacketSendAction> packet_send_action_;
    MultiSubscription subscriptions_;
  };

 public:
  TransportWriteStream(ActionContext action_context, ITransport& transport);
  ~TransportWriteStream() override;

  AE_CLASS_NO_COPY_MOVE(TransportWriteStream)

  ActionPtr<StreamWriteAction> Write(DataBuffer&& buffer) override;
  OutDataEvent::Subscriber out_data_event() override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;

 private:
  void GateUpdate();
  void ReceiveData(DataBuffer const& data, TimePoint current_time);
  void SetStreamInfo(ConnectionInfo const& connection_info);

  ActionContext action_context_;
  ITransport* transport_;

  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription transport_connection_subscription_;
  Subscription transport_disconnection_subscription_;
  Subscription transport_read_data_subscription_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_TRANSPORT_WRITE_STREAM_H_

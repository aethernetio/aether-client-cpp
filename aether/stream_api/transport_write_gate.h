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

#ifndef AETHER_STREAM_API_TRANSPORT_WRITE_GATE_H_
#define AETHER_STREAM_API_TRANSPORT_WRITE_GATE_H_

#include "aether/obj/ptr.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/transport/itransport.h"
#include "aether/transport/data_buffer.h"

#include "aether/stream_api/istream.h"

namespace ae {
class TransportWriteGate : public ByteIGate {
  class TransportStreamWriteAction : public StreamWriteAction {
   public:
    explicit TransportStreamWriteAction(
        ActionContext action_context,
        ActionView<PacketSendAction> packet_send_action);

    TimePoint Update(TimePoint current_time) override;
    void Stop() override;

   private:
    ActionView<PacketSendAction> packet_send_action_;
    MultiSubscription subscriptions_;
  };

 public:
  TransportWriteGate(ActionContext action_context, Ptr<ITransport> transport);

  TransportWriteGate(TransportWriteGate&& other) noexcept;

  ~TransportWriteGate() override;

  ActionView<StreamWriteAction> WriteIn(DataBuffer buffer,
                                        TimePoint current_time) override;
  OutDataEvent::Subscriber out_data_event() override;
  GateUpdateEvent::Subscriber gate_update_event() override;
  std::size_t max_write_in_size() const override;
  bool is_write_buffered() const override;
  std::size_t buffer_free_size() const override;
  bool is_linked() const override;

 private:
  void ReceiveData(DataBuffer const& data, TimePoint current_time);

  Ptr<ITransport> transport_;

  std::size_t max_data_size_;
  OutDataEvent out_data_event_;
  GateUpdateEvent gate_update_event_;

  Subscription transport_connection_subscription_;
  Subscription transport_read_data_subscription_;

  ActionList<TransportStreamWriteAction> write_actions_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_TRANSPORT_WRITE_GATE_H_
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

#ifndef AETHER_STREAM_API_SAFE_STREAM_H_
#define AETHER_STREAM_API_SAFE_STREAM_H_

#include "aether/common.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/safe_stream_sending.h"
#include "aether/stream_api/safe_stream/safe_stream_receiving.h"

#include "aether/stream_api/istream.h"

namespace ae {
class SafeStreamWriteAction final : public StreamWriteAction {
 public:
  explicit SafeStreamWriteAction(
      ActionContext action_context,
      ActionView<SendingDataAction> sending_data_action);

  TimePoint Update(TimePoint current_time) override;
  // TODO: add tests for stop
  void Stop() override;

 private:
  ActionView<SendingDataAction> sending_data_action_;
  MultiSubscription subscriptions_;
};

class SafeStream final : public ByteStream {
  class SafeStreamInGate final : public ByteGate {
   public:
    explicit SafeStreamInGate(
        ActionContext action_context,
        ActionView<SafeStreamSendingAction> safe_stream_sending,
        std::size_t max_data_size);

    ActionView<StreamWriteAction> Write(DataBuffer &&buffer,
                                        TimePoint current_time) override;

    void WriteOut(DataBuffer &&buffer, TimePoint current_time);

    void LinkOut(OutGate &gate) override;

    StreamInfo stream_info() const override;

   private:
    ActionList<SafeStreamWriteAction> packet_send_actions_;
    ActionView<SafeStreamSendingAction> safe_stream_sending_;
    StreamInfo stream_info_;
  };

  class SafeStreamOutGate final : public ByteGate {
   public:
    explicit SafeStreamOutGate(ProtocolContext &protocol_context);

    ActionView<StreamWriteAction> Write(DataBuffer &&buffer,
                                        TimePoint current_time) override;

    void LinkOut(OutGate &gate) override;

   private:
    ProtocolContext &protocol_context_;
  };

 public:
  SafeStream(ActionContext action_context, SafeStreamConfig config);

  AE_CLASS_NO_COPY_MOVE(SafeStream);

  ByteGate::Base &in() override;
  void LinkOut(OutGate &gate) override;

 private:
  void OnSendEvent(SafeStreamRingIndex offset, DataBuffer &&data,
                   TimePoint current_time);
  void OnRepeatEvent(SafeStreamRingIndex offset, std::uint16_t repeat_count,
                     DataBuffer &&data, TimePoint current_time);

  void OnConfirmEvent(SafeStreamRingIndex offset, TimePoint current_time);
  void OnRequestRepeatEvent(SafeStreamRingIndex offset, TimePoint current_time);

  void Confirm(MessageEventData<SafeStreamApi::Confirm> const &msg);
  void RequestRepeatSend(
      MessageEventData<SafeStreamApi::RequestRepeat> const &msg);
  void ReceiveSend(MessageEventData<SafeStreamApi::Send> const &msg);
  void ReceiveRepeat(MessageEventData<SafeStreamApi::Repeat> const &msg);

  ActionContext action_context_;

  ProtocolContext protocol_context_;
  SafeStreamSendingAction safe_stream_sending_;
  SafeStreamReceivingAction safe_stream_receiving_;

  SafeStreamInGate in_;
  SafeStreamOutGate out_;

  MultiSubscription subscriptions_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_H_

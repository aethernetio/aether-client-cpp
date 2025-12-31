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
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/safe_stream_recv_action.h"
#include "aether/stream_api/safe_stream/safe_stream_send_action.h"

#include "aether/stream_api/istream.h"

namespace ae {
class SafeStreamWriteAction final : public StreamWriteAction {
 public:
  explicit SafeStreamWriteAction(
      ActionContext action_context,
      ActionPtr<SendingDataAction> sending_data_action);

  // TODO: add tests for stop
  void Stop() override;

 private:
  ActionPtr<SendingDataAction> sending_data_action_;
  MultiSubscription subscriptions_;
};

class SafeStream final : public ByteStream,
                         public SafeStreamApiImpl,
                         public ISendDataPush,
                         public ISendAckRepeat {
 public:
  SafeStream(ActionContext action_context, SafeStreamConfig config);

  AE_CLASS_NO_COPY_MOVE(SafeStream);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;

  void LinkOut(OutStream& out) override;

  // Api impl methods
  void Ack(SSRingIndex::type offset) override;
  void RequestRepeat(SSRingIndex::type offset) override;
  void Send(SSRingIndex::type begin_offset, DataMessage data_message) override;

  // Implement ISendDataPush
  ActionPtr<StreamWriteAction> PushData(SSRingIndex begin,
                                        DataMessage&& data_message) override;

  // Implement ISendConfirmRepeat
  void SendAck(SSRingIndex offset) override;
  void SendRepeatRequest(SSRingIndex offset) override;

 private:
  void WriteOut(DataBuffer const& data);
  void OnStreamUpdate();
  void OnOutData(DataBuffer const& data);

  ActionContext action_context_;
  SafeStreamConfig config_;
  ProtocolContext protocol_context_;
  SafeStreamApi safe_stream_api_;
  ActionPtr<SafeStreamSendAction> send_action_;
  ActionPtr<SafeStreamRecvAction> recv_acion_;

  StreamInfo stream_info_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_H_

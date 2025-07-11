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
#include "aether/stream_api/safe_stream/safe_stream_action.h"

#include "aether/stream_api/istream.h"

namespace ae {
class SafeStreamWriteAction final : public StreamWriteAction {
 public:
  explicit SafeStreamWriteAction(
      ActionContext action_context,
      ActionView<SendingDataAction> sending_data_action);

  // TODO: add tests for stop
  void Stop() override;

 private:
  ActionView<SendingDataAction> sending_data_action_;
  MultiSubscription subscriptions_;
};

class SafeStream final : public ByteStream, public SafeStreamApiProvider {
 public:
  SafeStream(ActionContext action_context, SafeStreamConfig config);

  AE_CLASS_NO_COPY_MOVE(SafeStream);

  ActionView<StreamWriteAction> Write(DataBuffer &&data) override;
  StreamInfo stream_info() const override;

  void LinkOut(OutStream &out) override;

  ApiCallAdapter<SafeStreamApi> safe_stream_api() override;

 private:
  void WriteOut(DataBuffer const &data);
  void OnStreamUpdate();
  void OnOutData(DataBuffer const &data);

  ProtocolContext protocol_context_;
  SafeStreamAction safe_stream_action_;
  SafeStreamApi safe_stream_api_;

  ActionList<SafeStreamWriteAction> packet_send_actions_;

  StreamInfo stream_info_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_H_

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

#ifndef AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_H_
#define AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_H_

#include "aether/uid.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/debug_gate.h"
#include "aether/client_connections/client_to_server_stream.h"

namespace ae {

class MessageStream final : public ByteIStream {
 public:
  MessageStream(ClientToServerStream& client_to_server_stream, Uid destination);
  MessageStream(MessageStream const&) = delete;
  MessageStream(MessageStream&& other) noexcept;

  ActionView<StreamWriteAction> Write(DataBuffer&& data) override;

  StreamUpdateEvent::Subscriber stream_update_event() override;
  StreamInfo stream_info() const override;
  OutDataEvent::Subscriber out_data_event() override;

  void OnData(DataBuffer const& data);
  Uid destination() const;

 private:
  ClientToServerStream* client_to_server_stream_;
  Uid destination_;
  DebugGate debug_gate_;
  OutDataEvent out_data_event_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_H_

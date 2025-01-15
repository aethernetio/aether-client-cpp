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
#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/protocol_stream.h"
#include "aether/stream_api/debug_gate.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {

class MessageStream : public ByteStream {
 public:
  MessageStream(ProtocolContext& protocol_context, Uid destination);
  MessageStream(ProtocolContext& protocol_context, Uid destination,
                StreamId stream_id);
  MessageStream(MessageStream const&) = delete;
  MessageStream(MessageStream&& other) noexcept;

  InGate& in() override;
  void LinkOut(OutGate& out) override;

  Uid destination() const;
  StreamId stream_id() const;
  void set_stream_id(StreamId stream_id);

 private:
  ProtocolContext& protocol_context_;
  Uid destination_;
  StreamId stream_id_;
  ByteGate in_byte_gate_;
  ByteGate out_byte_gate_;
  DebugGate debug_gate_;
  StreamApiGate stream_api_gate_;
  ProtocolWriteGate open_stream_gate_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_MESSAGE_STREAM_H_

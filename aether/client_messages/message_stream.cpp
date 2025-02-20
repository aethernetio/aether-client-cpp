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

#include "aether/client_messages/message_stream.h"

#include "aether/methods/work_server_api/authorized_api.h"

#include "aether/tele/tele.h"

namespace ae {
MessageStream::MessageStream(ProtocolContext& protocol_context, Uid destination,
                             StreamId stream_id)
    : protocol_context_{protocol_context},
      destination_{destination},
      stream_id_{stream_id},
      in_byte_gate_{},
      out_byte_gate_{},
      debug_gate_{Format("MessageStream uid {} stream_id {} \nwrite {{}}",
                         destination_, static_cast<int>(stream_id_)),
                  Format("MessageStream uid {} stream_id {} \nread {{}}",
                         destination_, static_cast<int>(stream_id_))},
      stream_api_gate_{protocol_context_, stream_id_},
      open_stream_gate_{
          protocol_context_, AuthorizedApi{},
          AuthorizedApi::OpenStreamToClient{{}, destination_, stream_id_}} {
  AE_TELED_INFO("MessageStream create for stream: {} and destination: {}",
                static_cast<int>(stream_id_), destination_);
  Tie(in_byte_gate_, debug_gate_, stream_api_gate_, open_stream_gate_,
      out_byte_gate_);
}

MessageStream::MessageStream(MessageStream&& other) noexcept
    : protocol_context_{other.protocol_context_},
      destination_{other.destination_},
      stream_id_{other.stream_id_},
      in_byte_gate_{},
      out_byte_gate_{},
      debug_gate_{Format("MessageStream uid {} stream_id {} \nwrite {{}}",
                         destination_, static_cast<int>(stream_id_)),
                  Format("MessageStream uid {} stream_id {} \nread {{}}",
                         destination_, static_cast<int>(stream_id_))},
      stream_api_gate_{std::move(other.stream_api_gate_)},
      open_stream_gate_{std::move(other.open_stream_gate_)} {
  Tie(in_byte_gate_, debug_gate_, stream_api_gate_, open_stream_gate_,
      out_byte_gate_);
}

ByteStream::InGate& MessageStream::in() { return in_byte_gate_; }

void MessageStream::LinkOut(OutGate& out) { out_byte_gate_.LinkOut(out); }

Uid MessageStream::destination() const { return destination_; }

StreamId MessageStream::stream_id() const { return stream_id_; }

void MessageStream::set_stream_id(StreamId stream_id) {
  AE_TELED_INFO(
      "MessageStream change stream {} to stream: {} for destination: "
      "{}",
      static_cast<int>(stream_id_), static_cast<int>(stream_id), destination_);

  stream_id_ = stream_id;
  stream_api_gate_ = StreamApiGate{protocol_context_, stream_id_};
  open_stream_gate_ = ProtocolWriteGate{
      protocol_context_, AuthorizedApi{},
      AuthorizedApi::OpenStreamToClient{{}, destination_, stream_id_}};

  debug_gate_ =
      DebugGate{Format("MessageStream uid {} stream_id {} \nwrite {{}}",
                       destination_, static_cast<int>(stream_id_)),
                Format("MessageStream uid {} stream_id {} \nread {{}}",
                       destination_, static_cast<int>(stream_id_))};

  Tie(in_byte_gate_, debug_gate_, stream_api_gate_, open_stream_gate_,
      out_byte_gate_);
}

}  // namespace ae

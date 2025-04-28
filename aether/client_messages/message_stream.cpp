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

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
MessageStream::MessageStream(ClientToServerStream& client_to_server_stream,
                             Uid destination)
    : client_to_server_stream_{&client_to_server_stream},
      destination_{destination},
      debug_gate_{Format("MessageStream uid {}  \nwrite {{}}", destination_),
                  Format("MessageStream uid {} \nread {{}}", destination_)},
      inject_gate_{},
      send_message_gate_{
          client_to_server_stream_->protocol_context(),
          [&](ProtocolContext& context, auto&& data) {
            auto api_context =
                ApiContext{context, client_to_server_stream_->authorized_api()};
            api_context->send_message(destination_,
                                      std::forward<decltype(data)>(data));
            return DataBuffer{std::move(api_context)};
          }} {
  AE_TELE_INFO(kMessageStream, "MessageStream create to destination: {}",
               destination_);
  Tie(debug_gate_, inject_gate_, send_message_gate_);
}

MessageStream::MessageStream(MessageStream&& other) noexcept
    : client_to_server_stream_{other.client_to_server_stream_},
      destination_{other.destination_},
      debug_gate_{Format("MessageStream uid {}  \nwrite {{}}", destination_),
                  Format("MessageStream uid {} \nread {{}}", destination_)},
      inject_gate_{},
      send_message_gate_{
          client_to_server_stream_->protocol_context(),
          [&](ProtocolContext& context, auto&& data) {
            auto api_context =
                ApiContext{context, client_to_server_stream_->authorized_api()};
            api_context->send_message(destination_,
                                      std::forward<decltype(data)>(data));
            return DataBuffer{std::move(api_context)};
          }} {
  Tie(debug_gate_, inject_gate_, send_message_gate_);
}

ByteStream::InGate& MessageStream::in() { return debug_gate_; }

void MessageStream::LinkOut(OutGate& out) { send_message_gate_.LinkOut(out); }

void MessageStream::OnData(DataBuffer const& data) {
  inject_gate_.OutData(data);
}

Uid MessageStream::destination() const { return destination_; }
}  // namespace ae

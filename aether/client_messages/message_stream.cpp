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
                  Format("MessageStream uid {} \nread {{}}", destination_)} {
  AE_TELE_INFO(kMessageStream, "MessageStream create to destination: {}",
               destination_);
}

MessageStream::MessageStream(MessageStream&& other) noexcept
    : client_to_server_stream_{other.client_to_server_stream_},
      destination_{other.destination_},
      debug_gate_{Format("MessageStream uid {}  \nwrite {{}}", destination_),
                  Format("MessageStream uid {} \nread {{}}", destination_)} {}

ActionView<StreamWriteAction> MessageStream::Write(DataBuffer&& data) {
  auto gate_data = debug_gate_.WriteOut(std::move(data));
  auto auth_api = client_to_server_stream_->authorized_api_adapter();
  auth_api->send_message(destination_, std::move(gate_data));
  return auth_api.Flush();
}

MessageStream::StreamUpdateEvent::Subscriber
MessageStream::stream_update_event() {
  return client_to_server_stream_->stream_update_event();
}

StreamInfo MessageStream::stream_info() const {
  return client_to_server_stream_->stream_info();
}

MessageStream::OutDataEvent::Subscriber MessageStream::out_data_event() {
  return out_data_event_;
}

void MessageStream::OnData(DataBuffer const& data) {
  auto gate_data = debug_gate_.WriteOut(data);
  out_data_event_.Emit(gate_data);
}

Uid MessageStream::destination() const { return destination_; }
}  // namespace ae

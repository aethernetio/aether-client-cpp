/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/client_connections/cloud_message_stream.h"

#include "aether/tele/tele.h"

namespace ae {
CloudMessageStream::CloudMessageStream(
    ActionContext action_context, ClientConnectionManager& connection_manager)
    : action_context_{action_context},
      connection_manager_{&connection_manager} {
  SubscribeData();
}

ActionPtr<StreamWriteAction> CloudMessageStream::Write(AeMessage&& data) {
  AE_TELED_DEBUG("Write message to {} size: {}\ndata {}", data.uid,
                 data.data.size(), data.data);

  // TODO: add politics for send data
  auto& connection =
      connection_manager_->server_connections().front().ClientConnection();
  return connection.AuthorizedApiCall(
      SubApi{[&](ApiContext<AuthorizedApi>& api_context) {
        api_context->send_message(std::move(data));
      }});
}

CloudMessageStream::OutDataEvent::Subscriber
CloudMessageStream::out_data_event() {
  return out_data_event_;
}

StreamInfo CloudMessageStream::stream_info() const { return stream_info_; }

CloudMessageStream::StreamUpdateEvent::Subscriber
CloudMessageStream::stream_update_event() {
  return stream_update_event_;
}

void CloudMessageStream::Restream() {
  // TODO: add handle restream
  auto& connection =
      connection_manager_->server_connections().front().ClientConnection();
  connection.stream().Restream();
}

void CloudMessageStream::SubscribeData() {
  // TODO: add politics for receive data
  // TODO: add check if connection is valid
  // TODO: add subscribe to update connection
  auto& connection =
      connection_manager_->server_connections().front().ClientConnection();

  stream_info_ = connection.stream().stream_info();

  send_message_sub_ =
      connection.client_safe_api().send_messages_event().Subscribe(
          *this, MethodPtr<&CloudMessageStream::NewMessages>{});

  stream_update_sub_ = connection.stream().stream_update_event().Subscribe(
      *this, MethodPtr<&CloudMessageStream::UpdateStream>{});
}

void CloudMessageStream::NewMessages(std::vector<AeMessage> const& messages) {
  for (auto const& msg : messages) {
    AE_TELED_DEBUG("Message from {} size: {}\ndata {}", msg.uid,
                   msg.data.size(), msg.data);
    out_data_event_.Emit(msg);
  }
}

void CloudMessageStream::UpdateStream() {
  auto& connection =
      connection_manager_->server_connections().front().ClientConnection();
  stream_info_ = connection.stream().stream_info();
  stream_update_event_.Emit();
}

}  // namespace ae

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

#include "aether/transport/server/server_channel_stream.h"

#include "aether/actions/action_context.h"

#include "aether/tele/tele.h"

namespace ae {
// TODO: add config
static constexpr auto kBufferGateCapacity = std::size_t{20 * 1024};

ServerChannelStream::ServerChannelStream(ActionContext action_context,
                                         Adapter::ptr const& adapter,
                                         Server::ptr const& server,
                                         Channel::ptr const& channel)
    : action_context_{action_context},
      server_{server},
      channel_{channel},
      build_transport_action_{action_context, adapter, channel},
      buffer_stream_{action_context_, kBufferGateCapacity},
      connection_start_time_{Now()},
      connection_timer_{action_context_, channel->expected_connection_time()},
      build_transport_success_{build_transport_action_.ResultEvent().Subscribe(
          [this](auto& action) { OnConnected(action); })},
      build_transport_failed_{build_transport_action_.ErrorEvent().Subscribe(
          [this](auto&) { OnConnectedFailed(); })} {
  connection_timeout_ =
      connection_timer_.ResultEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("Connection timeout");
        OnConnectedFailed();
      });
}

ActionView<StreamWriteAction> ServerChannelStream::Write(DataBuffer&& data) {
  return buffer_stream_.Write(std::move(data));
}

ServerChannelStream::OutDataEvent::Subscriber
ServerChannelStream::out_data_event() {
  return buffer_stream_.out_data_event();
}
ServerChannelStream::StreamUpdateEvent::Subscriber
ServerChannelStream::stream_update_event() {
  return buffer_stream_.stream_update_event();
}
StreamInfo ServerChannelStream::stream_info() const {
  return buffer_stream_.stream_info();
}

void ServerChannelStream::OnConnected(
    BuildTransportAction& build_transport_action) {
  build_transport_success_.Reset();
  build_transport_failed_.Reset();

  auto channel_ptr = channel_.Lock();
  assert(channel_ptr);

  connection_timer_.Stop();
  auto connection_time =
      std::chrono::duration_cast<Duration>(Now() - connection_start_time_);
  channel_ptr->AddConnectionTime(connection_time);

  transport_ = build_transport_action.transport();
  ConnectTransportToStream();
}

void ServerChannelStream::OnConnectedFailed() {
  AE_TELED_ERROR("ServerChannelStream:OnConnectedFailed");
  connection_timeout_.Reset();
  build_transport_success_.Reset();
  build_transport_failed_.Reset();
  buffer_stream_.Unlink();
}

void ServerChannelStream::ConnectTransportToStream() {
  connection_error_ = transport_->ConnectionError().Subscribe(
      *this, MethodPtr<&ServerChannelStream::OnConnectedFailed>{});
  transport_write_gate_.emplace(action_context_, *transport_);
  Tie(buffer_stream_, *transport_write_gate_);
}

}  // namespace ae

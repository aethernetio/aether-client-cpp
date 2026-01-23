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

#include "aether/registration/root_server_select_stream.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether/tele/tele.h"

namespace ae {
RootServerSelectStream::RootServerSelectStream(
    ActionContext action_context, Ptr<RegistrationCloud> const& cloud)
    : action_context_{action_context}, cloud_{cloud}, server_index_{} {
  SelectServer();
}

ActionPtr<WriteAction> RootServerSelectStream::Write(DataBuffer&& data) {
  assert(server_connection_);
  return server_connection_->Write(std::move(data));
}

StreamInfo RootServerSelectStream::stream_info() const {
  return server_connection_ ? server_connection_->stream_info() : StreamInfo{};
}

RootServerSelectStream::StreamUpdateEvent::Subscriber
RootServerSelectStream::stream_update_event() {
  return stream_update_event_;
}

RootServerSelectStream::OutDataEvent::Subscriber
RootServerSelectStream::out_data_event() {
  return out_data_event_;
}

void RootServerSelectStream::Restream() {
  AE_TELED_ERROR("Restream RootServerSelectStream");
  if (server_connection_) {
    server_connection_->Restream();
    return;
  }
  CloudError();
}

RootServerSelectStream::ServerChangedEvent::Subscriber
RootServerSelectStream::server_changed_event() {
  return server_changed_event_;
}

RootServerSelectStream::CloudErrorEvent::Subscriber
RootServerSelectStream::cloud_error_event() {
  return cloud_error_event_;
}

void RootServerSelectStream::SelectServer() {
  auto cloud_ptr = cloud_.Lock();
  assert(cloud_ptr);

  if (server_index_ >= cloud_ptr->servers().size()) {
    CloudError();
    return;
  }
  auto& chosen_server = cloud_ptr->servers()[server_index_++];

  server_connection_.emplace(action_context_, chosen_server.Load());

  server_connection_->out_data_event().Subscribe(out_data_event_);
  server_connection_->server_error_event().Subscribe(
      MethodPtr<&RootServerSelectStream::ServerError>{this});

  server_changed_event_.Emit();
}

void RootServerSelectStream::ServerError() { SelectServer(); }

void RootServerSelectStream::CloudError() {
  AE_TELED_ERROR("Root server select stream error");
  cloud_error_event_.Emit();
}

}  // namespace ae
#endif

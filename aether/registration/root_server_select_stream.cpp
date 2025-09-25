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

#  include "aether/aether.h"

#  include "aether/tele/tele.h"

namespace ae {
RootServerSelectStream::RootServerSelectStream(
    ActionContext action_context, ObjPtr<Aether> const& aether,
    RegistrationCloud::ptr const& cloud)
    : action_context_{action_context},
      aether_{aether},
      cloud_{cloud},
      server_index_{} {
  SelectServer();
}

ActionPtr<StreamWriteAction> RootServerSelectStream::Write(DataBuffer&& data) {
  assert(server_stream_);
  return server_stream_->Write(std::move(data));
}

StreamInfo RootServerSelectStream::stream_info() const { return stream_info_; }

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
  if (server_stream_) {
    server_stream_->Restream();
    return;
  }
  StreamUpdateError();
}

RootServerSelectStream::ServerChangedEvent::Subscriber
RootServerSelectStream::server_changed_event() {
  return server_changed_event_;
}

void RootServerSelectStream::SelectServer() {
  RegistrationCloud::ptr cloud_ptr = cloud_.Lock();
  assert(cloud_ptr);

  if (server_index_ >= cloud_ptr->servers().size()) {
    StreamUpdateError();
    return;
  }
  auto chosen_server = cloud_ptr->servers()[server_index_++];

  ObjPtr<Aether> aether_ptr = aether_.Lock();
  assert(aether_ptr);

  server_stream_.emplace(action_context_, aether_ptr, chosen_server);
  stream_update_sub_ = server_stream_->stream_update_event().Subscribe(
      *this, MethodPtr<&RootServerSelectStream::StreamUpdate>{});
  out_data_sub_ = server_stream_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});

  StreamUpdate();
  server_changed_event_.Emit();
}

void RootServerSelectStream::StreamUpdate() {
  assert(server_stream_);
  stream_info_ = server_stream_->stream_info();
  if (stream_info_.link_state == LinkState::kLinkError) {
    SelectServer();
    return;
  }
  stream_update_event_.Emit();
}

void RootServerSelectStream::StreamUpdateError() {
  AE_TELED_ERROR("Root server select stream error");
  stream_info_.link_state = LinkState::kLinkError;
  stream_update_event_.Emit();
}

}  // namespace ae
#endif

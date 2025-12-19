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

#include "aether/registration/root_server_stream.h"

#if AE_SUPPORT_REGISTRATION

namespace ae {

RootServerStream::RootServerStream(ActionContext action_context,
                                   ObjPtr<Server> const& server)
    : action_context_{action_context},
      channel_manager_{action_context, server},
      channel_select_stream_{action_context, channel_manager_} {}

ActionPtr<StreamWriteAction> RootServerStream::Write(DataBuffer&& data) {
  return channel_select_stream_.Write(std::move(data));
}
StreamInfo RootServerStream::stream_info() const {
  return channel_select_stream_.stream_info();
}

RootServerStream::StreamUpdateEvent::Subscriber
RootServerStream::stream_update_event() {
  return channel_select_stream_.stream_update_event();
}

RootServerStream::OutDataEvent::Subscriber RootServerStream::out_data_event() {
  return channel_select_stream_.out_data_event();
}

void RootServerStream::Restream() {
  AE_TELED_DEBUG("Restream root server");
  channel_select_stream_.Restream();
}
}  // namespace ae

#endif

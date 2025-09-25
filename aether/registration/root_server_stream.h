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
#ifndef AETHER_REGISTRATION_ROOT_SERVER_STREAM_H_
#define AETHER_REGISTRATION_ROOT_SERVER_STREAM_H_

#include "aether/config.h"
#if AE_SUPPORT_REGISTRATION

#  include "aether/stream_api/istream.h"
#  include "aether/stream_api/buffer_stream.h"
#  include "aether/server_connections/channel_manager.h"
#  include "aether/server_connections/channel_selection_stream.h"

namespace ae {
class Aether;
class Server;

class RootServerStream final : public ByteIStream {
 public:
  RootServerStream(ActionContext action_context, ObjPtr<Aether> const& aether,
                   ObjPtr<Server> const& server);

  ActionPtr<StreamWriteAction> Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent::Subscriber stream_update_event() override;
  OutDataEvent::Subscriber out_data_event() override;
  void Restream() override;

 private:
  ActionContext action_context_;

  ChannelManager channel_manager_;
  BufferStream<DataBuffer> buffer_stream_;
  ChannelSelectStream channel_select_stream_;
};
}  // namespace ae
#endif
#endif  // AETHER_REGISTRATION_ROOT_SERVER_STREAM_H_

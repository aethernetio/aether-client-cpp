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

#include "aether/server_connections/channel_connection.h"

#include "aether/channel.h"

namespace ae {
ChannelConnection::ChannelConnection(ActionContext action_context,
                                     Channel::ptr const& channel)
    : action_context_{action_context}, channel_{channel} {}

ObjPtr<Channel> ChannelConnection::channel() const {
  Channel::ptr channel = channel_.Lock();
  return channel;
}

ServerChannel& ChannelConnection::GetServerChannel() {
  if (!server_channel_) {
    Channel::ptr channel = channel_.Lock();
    assert(channel);
    server_channel_ = std::make_unique<ServerChannel>(action_context_, channel);
  }
  return *server_channel_;
}

}  // namespace ae

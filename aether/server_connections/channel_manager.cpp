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

#include "aether/server_connections/channel_manager.h"

#include "aether/server.h"

#include "aether/tele/tele.h"

namespace ae {
ChannelManager::ChannelManager(ActionContext action_context,
                               ObjPtr<Aether> const& aether,
                               ObjPtr<Server> const& server)
    : action_context_(action_context), aether_(aether), server_(server) {
  AE_TELED_DEBUG("Create channel manager");
  InitChannels();
}

std::vector<ChannelConnection>& ChannelManager::channels() { return channels_; }

void ChannelManager::InitChannels() {
  auto server = server_.Lock();
  assert(server);

  // TODO: add update channels
  channels_.reserve(server->channels.size());
  for (auto const& channel : server->channels) {
    channels_.emplace_back(action_context_, channel);
  }
}

}  // namespace ae

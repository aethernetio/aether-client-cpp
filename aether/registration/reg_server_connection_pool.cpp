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

#include "aether/registration/reg_server_connection_pool.h"

#if AE_SUPPORT_REGISTRATION
#  include <utility>

#  include "aether/server.h"

namespace ae {
RegServerConnectionPool::RegServerConnectionPool(ActionContext action_context,
                                                 Cloud::ptr const& cloud)
    : action_context_{action_context},
      channels_{BuildChannels(cloud)},
      server_channel_loop_{AsyncForLoop<std::unique_ptr<ServerChannel>>{
          [this]() { channel_index_ = 0; },
          [this]() { return channel_index_ < channels_.size(); },
          [this]() { channel_index_++; },
          [this]() {
            auto channel = channels_[channel_index_].Lock();
            assert(channel);
            return std::make_unique<ServerChannel>(action_context_, channel);
          }}},
      current_server_channel_{server_channel_loop_.Update()} {}

ServerChannel* RegServerConnectionPool::TopConnection() {
  return current_server_channel_.get();
}

bool RegServerConnectionPool::Rotate() {
  current_server_channel_ = server_channel_loop_.Update();
  return !!current_server_channel_;
}

std::vector<PtrView<Channel>> RegServerConnectionPool::BuildChannels(
    Cloud::ptr const& cloud) {
  std::vector<PtrView<Channel>> channels;
  for (auto const& server : cloud->servers()) {
    for (auto const& channel : server->channels) {
      channels.emplace_back(channel);
    }
  }
  return channels;
}

}  // namespace ae
#endif

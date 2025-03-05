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

#include "aether/server_list/no_filter_server_list_policy.h"

#include <cassert>

#include "aether/server.h"

namespace ae {
bool NoFilterServerListPolicy::Preferred(ServerListItem const& left,
                                         ServerListItem const& right) const {
  // simple ordering comparison
  auto left_server = left.server();
  auto left_channel = left.channel();
  auto right_server = right.server();
  auto right_channel = right.channel();
  assert(left_server);
  assert(left_channel);
  assert(right_server);
  assert(right_channel);

  if (left_server == right_server) {
    for (auto const& i : left_server->channels) {
      if (i.get() == left_channel.get()) {
        // left first
        return true;
      }
      if (i.get() == right_channel.get()) {
        // right first
        break;
      }
    }
    return false;
  }

  return left_server->server_id < right_server->server_id;
}

bool NoFilterServerListPolicy::Filter(ServerListItem const& /* info */) const {
  return false;
}

}  // namespace ae

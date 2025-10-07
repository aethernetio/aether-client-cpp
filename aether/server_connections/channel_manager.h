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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_MANAGER_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_MANAGER_H_

#include <vector>

#include "aether/ptr/ptr_view.h"

#include "aether/server_connections/channel_connection.h"

namespace ae {
class Aether;
class Server;
class ChannelManager {
 public:
  ChannelManager(ActionContext action_context, ObjPtr<Aether> const& aether,
                 ObjPtr<Server> const& server);

  std::vector<ChannelConnection>& channels();

 private:
  void InitChannels();

  ActionContext action_context_;
  PtrView<Aether> aether_;
  PtrView<Server> server_;
  std::vector<ChannelConnection> channels_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_MANAGER_H_

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

#ifndef AETHER_REGISTRATION_REG_SERVER_CONNECTION_POOL_H_
#define AETHER_REGISTRATION_REG_SERVER_CONNECTION_POOL_H_

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include <vector>
#  include <cstddef>

#  include "aether/cloud.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/types/async_for_loop.h"
#  include "aether/actions/action_context.h"

#  include "aether/server_connections/server_channel.h"

namespace ae {
class RegServerConnectionPool {
 public:
  RegServerConnectionPool(ActionContext action_context,
                          Cloud::ptr const& cloud);

  ServerChannel* TopConnection();
  bool Rotate();

 private:
  static std::vector<PtrView<Channel>> BuildChannels(Cloud::ptr const& cloud);

  ActionContext action_context_;
  std::vector<PtrView<Channel>> channels_;
  std::size_t channel_index_;
  AsyncForLoop<std::unique_ptr<ServerChannel>> server_channel_loop_;
  std::unique_ptr<ServerChannel> current_server_channel_;
};
}  // namespace ae

#endif

#endif  // AETHER_REGISTRATION_REG_SERVER_CONNECTION_POOL_H_

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

#ifndef AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_

#include "aether/common.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action_context.h"

#include "aether/server_connections/server_channel.h"

namespace ae {
class Channel;
class ChannelConnection {
 public:
  ChannelConnection(ActionContext action_context,
                    ObjPtr<Channel> const& channel);

  AE_CLASS_MOVE_ONLY(ChannelConnection)

  ObjPtr<Channel> channel() const;

  /**
   * \brief Return ServerChannel.
   */
  std::unique_ptr<ServerChannel> GetServerChannel();

  void Reset();

  /**
   * \brief Connection penalty points increased during runtime
   */
  std::size_t connection_penalty;

 private:
  ActionContext action_context_;
  PtrView<Channel> channel_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CHANNEL_CONNECTION_H_

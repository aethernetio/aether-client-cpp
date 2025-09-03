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

#ifndef AETHER_SERVER_CONNECTIONS_BUILD_TRANSPORT_ACTION_H_
#define AETHER_SERVER_CONNECTIONS_BUILD_TRANSPORT_ACTION_H_

#include <vector>
#include <memory>
#include <cstdint>
#include <optional>

#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/types/async_for_loop.h"
#include "aether/events/event_subscription.h"

#include "aether/channel.h"
#include "aether/stream_api/istream.h"

namespace ae {

class BuildTransportAction final : public Action<BuildTransportAction> {
  enum class State : std::uint8_t {
    kGetBuilders,
    kConnect,
    kWaitForConnection,
    kConnected,
    kFailed,
  };

 public:
  BuildTransportAction(ActionContext action_context,
                       Channel::ptr const& channel);

  UpdateStatus Update();

  // return connected transport
  std::unique_ptr<ByteIStream> transport();

 private:
  void MakeBuilders();
  void Connect();

  PtrView<Channel> channel_;
  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders_;
  std::vector<std::unique_ptr<ITransportStreamBuilder>>::iterator it_;
  std::optional<AsyncForLoop<std::unique_ptr<ByteIStream>>> builder_loop_;
  std::unique_ptr<ByteIStream> transport_stream_;
  StateMachine<State> state_;
  Subscription state_changed_sub_;
  Subscription builders_sub_;
  Subscription connected_sub_;
  Subscription connection_failed_sub_;
};

}  // namespace ae
#endif  // AETHER_SERVER_CONNECTIONS_BUILD_TRANSPORT_ACTION_H_

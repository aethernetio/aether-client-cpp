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

#ifndef AETHER_AE_ACTIONS_TELEMETRY_H_
#define AETHER_AE_ACTIONS_TELEMETRY_H_

#include "aether/config.h"
#if AE_TELE_ENABLED
#  define TELEMETRY_ENABLED 1

#  include <optional>

#  include "aether/obj/obj_ptr.h"
#  include "aether/ptr/ptr_view.h"
#  include "aether/types/state_machine.h"
#  include "aether/actions/action.h"
#  include "aether/events/event_subscription.h"
#  include "aether/server_connections/client_to_server_stream.h"

#  include "aether/methods/telemetric.h"

namespace ae {
class Aether;
class Telemetry : public Action<Telemetry> {
  enum class State : std::uint8_t {
    kWaitRequest,
    kSendTelemetry,
    kStopped,
  };

 public:
  Telemetry(ActionContext action_context, ObjPtr<Aether> const& aether,
            ClientToServerStream& client_to_server);

  AE_CLASS_NO_COPY_MOVE(Telemetry)

  UpdateStatus Update();

  void Stop();

  void SendTelemetry();

 private:
  void OnRequestTelemetry();
  std::optional<Telemetric> CollectTelemetry(StreamInfo const& stream_info);

  PtrView<Aether> aether_;
  ClientToServerStream* client_to_server_;

  Subscription telemetry_request_sub_;
  StateMachine<State> state_;
};
}  // namespace ae
#endif
#endif  // AETHER_AE_ACTIONS_TELEMETRY_H_

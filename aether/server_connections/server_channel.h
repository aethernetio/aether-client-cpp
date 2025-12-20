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

#ifndef AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_
#define AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_

#include <cassert>

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/istream.h"
#include "aether/transport/transport_builder_action.h"

namespace ae {
class Channel;

class ServerChannel final {
 public:
  // Connected or not
  using ConnectionResult = Event<void(bool is_connected)>;

  ServerChannel(ActionContext action_context, ObjPtr<Channel> const& channel);

  AE_CLASS_NO_COPY_MOVE(ServerChannel)

  /**
   * \brief Get the channel stream. May be null.
   */
  ByteIStream* stream();
  ObjPtr<Channel> channel() const;
  ConnectionResult::Subscriber connection_result();

 private:
  void OnTransportCreated(TransportBuilderAction& transport_builder_action);
  void OnTransportCreateFailed();

  ActionContext action_context_;
  PtrView<Channel> channel_;

  std::unique_ptr<ByteIStream> transport_stream_;

  ActionPtr<TransportBuilderAction> build_transport_action_;
  Subscription build_transport_sub_;

  ActionPtr<TimerAction> transport_build_timer_;
  Subscription transport_build_timer_sub_;
  Subscription connection_error_;

  ConnectionResult connection_result_event_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_SERVER_CHANNEL_H_

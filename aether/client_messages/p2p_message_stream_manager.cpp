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

#include "aether/client_messages/p2p_message_stream_manager.h"

#include <cassert>
#include <utility>

#include "aether/client.h"
#include "aether/client_messages/client_messages_tele.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"

namespace ae {

P2pMessageStreamManager::P2pMessageStreamManager(AeContext const& ae_context,
                                                 Ptr<Client> const& client)
    : ae_context_{ae_context},
      client_{client},
      cloud_connection_{&client->cloud_connection()},
      on_message_received_sub_{CloudEventListener{
          ApiEventSubscriber{[this](ClientApiSafe& client_api, auto*) {
            return client_api.send_message_event().Subscribe(
                MethodPtr<&P2pMessageStreamManager::NewMessageReceived>{this});
          }},
          *cloud_connection_,
          RequestPolicy::Replica{cloud_connection_->count_connections()}}} {}

P2pPortHandle P2pMessageStreamManager::CreatePort(Uid const& destination) {
  auto [port, is_new] = GetOrCreatePort(destination);
  return P2pPortHandle{std::move(port)};
}

P2pMessageStreamManager::NewPortEvent::Subscriber
P2pMessageStreamManager::new_port_event() {
  return EventSubscriber{new_port_event_};
}

std::pair<std::shared_ptr<p2p_stream_internal::P2pReceivePort>, bool>
P2pMessageStreamManager::GetOrCreatePort(Uid const& destination) {
  CleanUpPorts();
  auto it = ports_.find(destination);
  if (it != std::end(ports_)) {
    if (auto existing = it->second.lock()) {
      return {std::move(existing), false};
    }
  }
  // Do not use std::make_shared here. GCC 16/libstdc++ emits a false
  // -Warray-bounds warning while destroying P2pReceivePort from
  // _Sp_counted_ptr_inplace because P2pReceivePort owns Event/RcPtr storage.
  auto port = std::shared_ptr<p2p_stream_internal::P2pReceivePort>{
      std::make_unique<p2p_stream_internal::P2pReceivePort>(destination)};
  ports_.emplace(destination, port);
  return {port, true};
}

void P2pMessageStreamManager::NewMessageReceived(AeMessage const& message) {
  AE_TELED_DEBUG("New message received {}", message.uid);

  auto [port, is_new] = GetOrCreatePort(message.uid);
  assert(port != nullptr);

  if (is_new) {
    new_port_event_.Emit(P2pPortHandle{port});
  }

  port->Deliver(message.data);
}

void P2pMessageStreamManager::CleanUpPorts() {
  std::erase_if(ports_, [](auto const& p) { return p.second.expired(); });
}

}  // namespace ae

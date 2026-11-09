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
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/diagnostics/bench_connection_event_diag.h"
#include "aether/work_cloud_api/client_api/client_api_safe.h"

#include "aether/client_messages/client_messages_tele.h"

#include <cstring>

namespace ae {
namespace {

#if AE_BENCH_CONNECTION_EVENT_DIAG
std::uint64_t UidLo64(Uid const& uid) noexcept {
  std::uint64_t key = 0;
  static_assert(Uid::kSize >= sizeof(key));
  std::memcpy(&key, uid.value.data(), sizeof(key));
  return key;
}
#endif

}  // namespace

P2pMessageStreamManager::P2pMessageStreamManager(AeContext const& ae_context,
                                                 Ptr<Client> const& client)
    : ae_context_{ae_context},
      client_{client},
      cloud_connection_{&client->cloud_connection()},
      new_port_event_{ae_context_},
      on_message_received_sub_{CloudEventListener{
          ApiEventSubscriber{[this](ClientApiSafe& client_api,
                                    CloudServerConnection* server_connection)
                                 -> RegEventHandler {
            auto const server_id = server_connection != nullptr
                                         ? server_connection->server_id()
                                         : ServerId{};
#if AE_BENCH_CONNECTION_EVENT_DIAG
            std::uint64_t const cloud_inst =
                server_connection != nullptr
                    ? server_connection->bench_cloud_instance_id()
                    : 0U;
            AE_BENCH_CONN_DIAG("send_message_sub", server_id, cloud_inst, 0, 0,
                               0, 0, 1, 0, -1);
#endif
            return client_api.send_message_event().Subscribe(
                [this, server_id](AeMessage const& message) {
#if AE_BENCH_CONNECTION_EVENT_DIAG
                  bench_conn_event_diag::SetCurrentInboundMessageServerId(
                      server_id);
                  AE_BENCH_CONN_DIAG(
                      "send_message_event", server_id, 0,
                      UidLo64(message.uid),
                      static_cast<std::uint32_t>(message.data.size()), 0, 0, -1,
                      0, -1);
#endif
                  NewMessageReceived(message);
                });
          }},
          *cloud_connection_,
          // Receive on every linked registered cloud server (including soft
          // ping quarantine). RequestPolicy::All send routing is unchanged.
          RequestPolicy::Variant{RequestPolicy::AllRegisteredServers{}}}} {}

P2pPortHandle P2pMessageStreamManager::CreatePort(Uid const& destination) {
  auto [port, is_new] = GetOrCreatePort(destination);
  return P2pPortHandle{std::move(port)};
}

P2pMessageStreamManager::NewPortEvent const&
P2pMessageStreamManager::new_port_event() {
  return new_port_event_;
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
      std::make_unique<p2p_stream_internal::P2pReceivePort>(ae_context_,
                                                            destination)};
  ports_.emplace(destination, port);
  return {port, true};
}

void P2pMessageStreamManager::NewMessageReceived(AeMessage const& message) {
  AE_TELED_DEBUG("New message received {}", message.uid);
  auto [port, is_new] = GetOrCreatePort(message.uid);
  assert(port != nullptr);

  bool new_port_emitted = false;
  if (is_new) {
    auto handle = P2pPortHandle{port};
    new_port_event_.Emit(handle);
    new_port_emitted = true;
  }

#if AE_BENCH_CONNECTION_EVENT_DIAG
  std::uint64_t receiver_uid = 0;
  if (auto client = client_.Lock()) {
    receiver_uid = UidLo64(client->uid());
  }
  AE_BENCH_CONN_DIAG(
      "p2p_new_message_rx", bench_conn_event_diag::CurrentInboundMessageServerId(),
      receiver_uid, UidLo64(message.uid),
      static_cast<std::uint32_t>(message.data.size()),
      static_cast<std::uint32_t>(message.data.size()), is_new ? 1U : 0U,
      new_port_emitted ? 1 : 0, 1, -1);
#endif

  port->Deliver(message.data);
}

void P2pMessageStreamManager::CleanUpPorts() {
  std::erase_if(ports_, [](auto const& p) { return p.second.expired(); });
}

}  // namespace ae

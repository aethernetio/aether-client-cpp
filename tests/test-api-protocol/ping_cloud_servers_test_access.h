/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef TESTS_TEST_API_PROTOCOL_PING_CLOUD_SERVERS_TEST_ACCESS_H_
#define TESTS_TEST_API_PROTOCOL_PING_CLOUD_SERVERS_TEST_ACCESS_H_

#include "aether/cloud_connections/ping_cloud_servers.h"

#if AE_ENABLE_PING
namespace ae {
struct PingCloudServersTestAccess {
  static void CancelPeriodicStart(PingCloudServers& pcs, ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    if (it != pcs.server_pings_.end()) {
      it->second->start_sub_.Reset();
    }
  }

  static void TriggerPeriodicStart(PingCloudServers& pcs, ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    if (it != pcs.server_pings_.end()) {
      it->second->Start();
    }
  }

  static bool HasPendingClientPing(PingCloudServers& pcs, ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    return it != pcs.server_pings_.end() && it->second->ping_.has_value() &&
           it->second->ping_->IsPending();
  }

  static bool HasAwaitingLateClientPing(PingCloudServers& pcs,
                                        ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    return it != pcs.server_pings_.end() && it->second->AwaitingLatePing();
  }

  static Ping const* ClientPingPtr(PingCloudServers& pcs, ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    if (it == pcs.server_pings_.end() || !it->second->ping_.has_value()) {
      return nullptr;
    }
    return &*it->second->ping_;
  }

  static void StopServerPing(PingCloudServers& pcs, ServerId server_id) {
    auto it = pcs.server_pings_.find(server_id);
    if (it != pcs.server_pings_.end()) {
      it->second->Stop();
    }
  }
};
}  // namespace ae
#endif  // AE_ENABLE_PING

#endif  // TESTS_TEST_API_PROTOCOL_PING_CLOUD_SERVERS_TEST_ACCESS_H_

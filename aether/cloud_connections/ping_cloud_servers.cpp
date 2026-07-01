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

#include "aether/cloud_connections/ping_cloud_servers.h"

#if AE_ENABLE_PING

#  include "aether/server.h"
#  include "aether/channels/channel.h"

#  include "aether/cloud_connections/cloud_connections_tele.h"

namespace ae {
PingCloudServers::PingCloudServers(
    AeContext const& ae_context,
    CloudServerConnections& cloud_server_connections)
    : ae_context_{ae_context},
      cloud_server_connections_{&cloud_server_connections},
      servers_update_{
          cloud_server_connections_->servers_update_event().Subscribe(
              MethodPtr<&PingCloudServers::ServersUpdate>{this})} {
  AE_TELED_INFO("PingCloudServers created");
  ServersUpdate();
}

void PingCloudServers::ServersUpdate() {
  AE_TELED_DEBUG("Servers update");
  // enqueue redispatch for servers
  if (!task_sub_) {
    AE_TELED_DEBUG("Enqueue dispatch servers");
    task_sub_ = ae_context_.scheduler().Task([this]() { DispatchToServers(); });
  }
}

void PingCloudServers::DispatchToServers() {
  cloud_server_connections_->ForServers(
      [this](CloudServerConnection* cloud_sc) {
        if (cloud_sc == nullptr) {
          AE_TELED_ERROR("Visit empty cloud server connection!");
          return;
        }
        MakePingToServer(*cloud_sc);
      },
      // TODO: config request policy
      RequestPolicy::All{});
}

void PingCloudServers::MakePingToServer(CloudServerConnection& cloud_sc) {
  auto [sid, server_ping, is_new] = GetOrCreatePing(cloud_sc);
  AE_TELED_DEBUG("Make ping to server {}, is_new {}", sid, is_new);
  if (!is_new) {
    return;
  }

  auto c = cloud_sc.client_connection()->server_connection().current_channel();
  if (!c) {
    AE_TELED_ERROR("Current channel value invalid");
    return;
  }
  auto timeout = c->ResponseTimeout();
  constexpr auto kDefaultInterval =
      std::chrono::milliseconds{AE_PING_INTERVAL_MS};
  constexpr auto kDefaultRxWindow = kDefaultInterval;

  // TODO: different ping interval depends on priority
  server_ping.ping = std::make_unique<Ping>(
      ae_context_, cloud_sc, kDefaultInterval, kDefaultRxWindow, timeout);

  // subscribe to ping result
  // if success get ping time from result and save for statistics
  // if failed request Restream for what server connection
  server_ping.ping->result_event().Subscribe([this, sid_ = sid,
                                              csc_ = &cloud_sc](auto res) {
    auto it = server_pings_.find(sid_);
    if (it == server_pings_.end()) {
      return;
    }
    auto& sp = it->second;
    if (res) {
      // successful ping save timeout to the current channel
      auto c = csc_->client_connection()->server_connection().current_channel();
      if (!c) {
        AE_TELED_ERROR("Ping's channel value invalid");
        return;
      }
      c->channel_statistics().AddResponseTime(res.value());
      // and update timeout value
      sp.ping->SetTimeout(c->ResponseTimeout());
    } else {
      // ping error
      AE_TELED_ERROR("Ping error!");
      // after restream if server is changed
      csc_->Restream();
    }
  });
  // reset ping on server error
  server_ping.server_state_sub = cloud_sc.client_connection()
                                     ->server_connection()
                                     .server_error_event()
                                     .Subscribe([this, sid_ = sid]() {
                                       auto it = server_pings_.find(sid_);
                                       if (it == server_pings_.end()) {
                                         return;
                                       }
                                       it->second.ping.reset();
                                     });
}

auto PingCloudServers::GetOrCreatePing(CloudServerConnection& cloud_sc)
    -> std::tuple<ServerId, ServerPing&, bool> {
  auto server = cloud_sc.server();
  assert(server && "Server must exists");

  auto server_id = server->server_id;
  auto it = server_pings_.find(server_id);
  // Create new ping or replace ping if server's priority changed
  if ((it == server_pings_.end()) ||
      (it->second.priority != cloud_sc.priority())) {
    auto [new_it, _] = server_pings_.insert_or_assign(
        server_id, ServerPing{
                       .ping{},
                       .priority = cloud_sc.priority(),
                       .server_state_sub = {},
                   });
    return {server_id, new_it->second, true};
  }

  // if ping not has value return is_new
  return {server_id, it->second, !it->second.ping};
}

}  // namespace ae

#endif

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
#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "aether-objects/ptr/ptr.h"
#include "aether-objects/ptr/ptr_view.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"

#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_callbacks.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/server_connections/iserver_connection_factory.h"

namespace ae {

namespace cloud_server_connections_internal {
class EmptyConnectionsWA final : public WriteAction {
 public:
  explicit EmptyConnectionsWA(AeContext const& ae_context);
};

class ReplicaWA final : public WriteAction {
 public:
  explicit ReplicaWA(std::vector<WriteAction*>&& swas) noexcept;
  void Stop() noexcept override;

 private:
  std::vector<WriteAction*> swas_;
  std::size_t failed_actions_{};
  std::size_t stopped_actions_{};
  MultiSubscription subs_;
};
}  // namespace cloud_server_connections_internal

class CloudServerConnections {
  friend struct CloudServerConnectionsTestAccess;

 public:
  using ServersUpdate = Event<void()>;
  using ServerQuarantineEvent = Event<void(CloudServerConnection*)>;

  CloudServerConnections(
      AeContext const& ae_context, Ptr<Cloud> const& cloud,
      std::unique_ptr<IServerConnectionFactory> connection_factory,
      std::size_t max_connections);
  /**
   * \brief The event then top list of the servers were updated.
   */
  ServersUpdate::Subscriber servers_update_event();
  ServerQuarantineEvent::Subscriber server_quarantined_event();
  ServerQuarantineEvent::Subscriber server_quarantine_release_event();
  /**
   * \brief List of currently selected servers in priority order
   */
  std::vector<CloudServerConnection*> const& selected_servers() const;
  /**
   * \brief List of all server connections in canonical priority order.
   *
   * The list contains selected servers in selected_servers() order, followed
   * by usable non-selected servers, then quarantined servers from oldest to
   * newest quarantine. A server's persisted priority equals its index.
   */
  std::vector<CloudServerConnection*> const& servers();

  std::size_t count_connections() const;
  std::size_t max_connections() const;

  /**
   * \brief Restream all selected servers
   */
  void Restream();

  /**
   * \brief Iterate over servers according to the request policy.
   * Calls func with CloudServerConnection* for each server.
   */
  template <typename TFunc>
  void ForServers(TFunc&& func,
                  RequestPolicy::Variant policy = RequestPolicy::MainServer{}) {
    std::visit([&](auto p) { ForServersImpl(std::forward<TFunc>(func), p); },
               policy);
  }

  /**
   * \brief Fire-and-forget API call to servers per policy.
   * Returns a WriteAction that reflects the write status.
   */
  WriteAction& CallApi(
      ApiCall const& api_caller,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});

 private:
  template <typename TFunc>
  void ForServersImpl(TFunc&& func, RequestPolicy::MainServer) {
    if (selected_servers_.empty()) {
      return;
    }
    std::invoke(std::forward<TFunc>(func), selected_servers_.front());
  }

  template <typename TFunc>
  void ForServersImpl(TFunc&& func, RequestPolicy::Priority priority) {
    if (priority.priority >= selected_servers_.size()) {
      return;
    }
    std::invoke(std::forward<TFunc>(func),
                selected_servers_[priority.priority]);
  }

  template <typename TFunc>
  void ForServersImpl(TFunc&& func, RequestPolicy::Replica replica) {
    auto&& callable = std::forward<TFunc>(func);
    auto visit_count = std::min(selected_servers_.size(), replica.count);
    for (std::size_t i = 0; i < visit_count; ++i) {
      std::invoke(callable, selected_servers_[i]);
    }
  }

  template <typename TFunc>
  void ForServersImpl(TFunc&& func, RequestPolicy::All) {
    auto&& callable = std::forward<TFunc>(func);
    for (auto* sc : selected_servers_) {
      std::invoke(callable, sc);
    }
  }

  WriteAction& EmptyWriteAction();
  WriteAction& ReplicaWriteAction(std::vector<WriteAction*>&& swas);

  struct ServerEntry {
    ServerEntry(Ptr<Cloud> const& cloud, ServerId server_id,
                IServerConnectionFactory& connection_factory)
        : connection{cloud, server_id, connection_factory} {}

    CloudServerConnection connection;
    Subscription state_sub;
    Subscription error_sub;
    TaskSubscription quarantine_sub;
  };

  void InitServerConnections();
  // Caller must unsubscribe before re-subscribing the same server.
  bool SubscribeToServerState(CloudServerConnection& server_connection);
  void UnsubscribeFromServerState(CloudServerConnection& server_connection);
  bool QuarantineServer(CloudServerConnection& server_connection);
  void QuarantineAndReconcile(CloudServerConnection& server_connection);
  void ReleaseQuarantinedServer(CloudServerConnection& server_connection);
  void ScheduleReconcileServers();
  void ReconcileServers();

  void NormalizeServerPriorities();
  ServerEntry& ServerEntryFor(CloudServerConnection& server_connection);

  AeContext ae_context_;
  PtrView<Cloud> cloud_;
  std::unique_ptr<IServerConnectionFactory> connection_factory_;
  std::size_t max_connections_;
  // Entries are reserved and populated once before connection pointers are
  // published. Do not grow or rebuild this vector: callbacks retain pointers
  // to its connections.
  std::vector<ServerEntry> server_entries_;

  // Canonical priority order: selected prefix, usable non-selected servers,
  // then quarantined servers oldest-to-newest. Persisted priorities equal
  // all_servers_ indices.
  std::vector<CloudServerConnection*> all_servers_;
  // Selected prefix of all_servers_, in selection and priority order.
  std::vector<CloudServerConnection*> selected_servers_;

  ServersUpdate servers_update_event_;
  ServerQuarantineEvent server_quarantined_event_;
  ServerQuarantineEvent server_quarantine_release_event_;

  TaskSubscription defer_sub_;

  std::optional<cloud_server_connections_internal::EmptyConnectionsWA>
      empty_wa_;
  std::vector<cloud_server_connections_internal::ReplicaWA> replica_was_;
};
}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_

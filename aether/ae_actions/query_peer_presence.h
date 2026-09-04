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

#ifndef AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_
#define AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "aether-miscpp/types/result.h"
#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/events/multi_subscription.h"
#include "aether/remote_presence.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"

namespace ae {

class Client;

enum class QueryPeerPresenceError : int {
  kGetCloudFailed = 1,
  kNoWorkServerAvailable = 2,
  kGetClientTimingFailed = 3,
};

// Asynchronous Remote Presence over peer Personal Cloud authoritative servers.
// Never falls back to the observer/requester own cloud.
class QueryPeerPresence final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerPresence, int>)>;

  QueryPeerPresence(AeContext const& ae_context, Client& client, Uid peer_uid);
  ~QueryPeerPresence() override;

  AE_CLASS_NO_COPY_MOVE(QueryPeerPresence)

  ResultEvent::Subscriber result_event() noexcept;
  Uid peer_uid() const noexcept { return peer_uid_; }
  std::vector<RemoteServerPresenceSample> const& samples() const noexcept {
    return samples_;
  }
  std::vector<ServerId> const& peer_cloud_server_ids() const noexcept {
    return peer_cloud_server_ids_;
  }
  std::vector<ServerId> const& authoritative_server_ids() const noexcept {
    return authoritative_server_ids_;
  }
  std::vector<ServerId> const& queried_server_ids() const noexcept {
    return queried_server_ids_;
  }
  bool used_observer_cloud() const noexcept { return used_observer_cloud_; }

 private:
  struct AttemptMeta {
    std::uint64_t next_generation{0};
    // Per-attempt send times so late responses from earlier attempts remain
    // classifiable after a soft-timeout retry is launched.
    std::map<std::uint64_t, TimePoint> send_times;
  };

  void OnCloud(Result<Cloud::ptr, int> result);
  void BindPeerCloud(Cloud::ptr cloud);
  void StartQuery();
  void RefreshUsableSet();
  void RequestTiming(CloudServerConnection* sc);
  void OnServerTiming(CloudServerConnection* sc, std::uint64_t generation,
                      Result<ClientTiming, std::int32_t> const& res);
  void MarkUnknown(ServerId server_id);
  void MarkExcluded(ServerId server_id);
  void OnServerRecovered(CloudServerConnection* sc);
  void MaybeComplete();
  void Complete(PeerPresence const& presence);
  void Fail(int code);
  Duration OfflineTimeout() const noexcept;
  CloudRequestExecutionPolicy ExecutionPolicy() const noexcept;
  bool AllUsableTerminal() const noexcept;

  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ResultEvent result_event_;

  Subscription get_cloud_sub_;
  Subscription cloud_request_sub_;
  Subscription exhausted_sub_;
  Subscription quarantine_sub_;
  Subscription quarantine_release_sub_;

  std::unique_ptr<CloudServerConnections> dest_cloud_;
  CloudServerConnections* work_cloud_{nullptr};
  std::optional<CloudRequest> cloud_request_;
  // MultiSubscription so soft-timeout retries do not destroy earlier
  // get_client_timing response subscribers (late responses must be accepted).
  std::map<ServerId, MultiSubscription> timing_subs_;
  std::map<ServerId, AttemptMeta> attempts_;
  std::vector<RemoteServerPresenceSample> samples_;
  std::vector<ServerId> peer_cloud_server_ids_;
  std::vector<ServerId> authoritative_server_ids_;
  std::vector<ServerId> queried_server_ids_;
  bool used_observer_cloud_{false};
  bool finished_{false};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_

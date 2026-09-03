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
#include "aether/remote_presence.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

namespace ae {

class Client;

enum class QueryPeerPresenceError : int {
  kGetCloudFailed = 1,
  kNoWorkServerAvailable = 2,
  kGetClientTimingFailed = 3,
};

// Asynchronous Remote Presence over authoritative usable servers.
// Aggregation: Offline = any Offline; Online = all usable Online;
// Unknown = zero usable or incomplete Online set without Offline.
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

 private:
  struct AttemptMeta {
    TimePoint send_time{};
    std::uint64_t generation{0};
    std::size_t retries_used{0};
  };

  void OnCloud(Result<Cloud::ptr, int> result);
  void StartQuery();
  void RefreshUsableSet();
  void OnServerTiming(CloudServerConnection* sc, std::uint64_t generation,
                      Result<ClientTiming, std::int32_t> const& res);
  void MarkUnknown(ServerId server_id);
  void MarkExcluded(ServerId server_id);
  void MaybeComplete();
  void Complete(PeerPresence const& presence);
  void Fail(int code);
  Duration OfflineTimeout() const noexcept;

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
  std::map<ServerId, Subscription> timing_subs_;
  std::map<ServerId, AttemptMeta> attempts_;
  std::vector<RemoteServerPresenceSample> samples_;
  bool finished_{false};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_

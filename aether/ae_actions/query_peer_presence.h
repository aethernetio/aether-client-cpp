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
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "aether-miscpp/types/result.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/receive_schedule.h"
#include "aether/types/uid.h"
namespace ae {
class Client;
enum class QueryPeerPresenceError : int {
  kGetCloudFailed = 1,
  kNoWorkServerAvailable = 2,
  kGetClientTimingFailed = 3,
};
// CLOUD-SCOPED presence aggregation over the peer's relevant servers.
// Offline: ANY server MissedDeadline (early completion).
// Online: all relevant observations complete, no MissedDeadline, >=1 Expected.
// Unknown: otherwise. Does not use observer receive_window / ping_interval.
class QueryPeerPresence final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerPresence, int>)>;
  QueryPeerPresence(AeContext const& ae_context, Client& client, Uid peer_uid);
  ~QueryPeerPresence() override;
  AE_CLASS_NO_COPY_MOVE(QueryPeerPresence)
  ResultEvent::Subscriber result_event() noexcept;
  std::vector<ServerTimingDiagnostic> const& server_diagnostics() const noexcept;
  PeerTimingQueryCoverage coverage() const noexcept;
  Uid peer_uid() const noexcept { return peer_uid_; }
  // Library Now() when the first Expected server response was applied, if any.
  std::optional<TimePoint> first_expected_time() const noexcept {
    return first_expected_time_;
  }
  // Library Now() when the first MissedDeadline server response was applied.
  std::optional<TimePoint> first_missed_time() const noexcept {
    return first_missed_time_;
  }
  // Library Now() when the last relevant server observation completed
  // (Success or TerminalError) before / at presence completion.
  std::optional<TimePoint> last_server_completion_time() const noexcept {
    return last_server_completion_time_;
  }
  // Library Now() when the user presence callback was emitted.
  std::optional<TimePoint> completed_at() const noexcept {
    return completed_at_;
  }
 private:
  Duration OneWayEstimateFor(CloudServerConnection* sc) const;
  void OnCloud(Result<Cloud::ptr, int> result);
  void SnapshotExpectedServers();
  void StartQuery();
  void OnServerTiming(CloudServerConnection* sc, std::uint64_t send_generation,
                      Result<ClientTiming, std::int32_t> const& res);
  void MaybeComplete();
  void Complete(PeerPresence const& presence);
  void Fail(int code);
  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ResultEvent result_event_;
  Subscription get_cloud_sub_;
  Subscription cloud_request_sub_;
  Subscription exhausted_sub_;
  std::unique_ptr<CloudServerConnections> dest_cloud_;
  CloudServerConnections* work_cloud_{nullptr};
  std::optional<CloudRequest> cloud_request_;
  std::map<ServerId, Subscription> timing_subs_;
  PeerTimingQueryState query_state_{};
  std::vector<ServerTimingDiagnostic> diagnostics_;
  std::optional<TimePoint> first_expected_time_;
  std::optional<TimePoint> first_missed_time_;
  std::optional<TimePoint> last_server_completion_time_;
  std::optional<TimePoint> completed_at_;
  bool finished_{false};
};
}  // namespace ae
#endif  // AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_

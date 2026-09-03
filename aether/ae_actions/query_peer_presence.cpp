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

#include "aether/ae_actions/query_peer_presence.h"

#include <algorithm>
#include <utility>

#include "aether/api_protocol/sub_api.h"
#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/config.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/server.h"
#include "aether/tele.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {

QueryPeerPresence::QueryPeerPresence(AeContext const& ae_context,
                                     Client& client, Uid peer_uid)
    : ae_context_{ae_context}, client_{&client}, peer_uid_{peer_uid} {
  static_cast<void>(AllowObserverCloudFallbackForPeerPresence());

  auto cached = client_->cloud_manager()->GetCachedCloud(peer_uid_);
  if (cached && cached.is_valid() && !cached->servers().empty()) {
    BindPeerCloud(cached);
    StartQuery();
    return;
  }

  auto& get_cloud = client_->cloud_manager()->GetCloud(peer_uid_);
  get_cloud_sub_ = get_cloud.result_event().Subscribe(
      [this](Result<Cloud::ptr, int> result) { OnCloud(std::move(result)); });
}

QueryPeerPresence::~QueryPeerPresence() {
  finished_ = true;
  timing_subs_.clear();
}

QueryPeerPresence::ResultEvent::Subscriber
QueryPeerPresence::result_event() noexcept {
  return EventSubscriber{result_event_};
}

Duration QueryPeerPresence::OfflineTimeout() const noexcept {
  auto policy = client_->connectivity_policy();
  if (!policy) {
    return DefaultOfflineDetectionTimeout();
  }
  return policy.Load()->offline_detection_timeout();
}

void QueryPeerPresence::BindPeerCloud(Cloud::ptr cloud) {
  used_observer_cloud_ = false;

  // Full peer Personal Cloud (priority order). Authoritative Presence set is
  // later taken from selected_servers() — same contract as Local Presence.
  peer_cloud_server_ids_.clear();
  std::vector<std::pair<std::uint16_t, ServerId>> ordered;
  ordered.reserve(cloud->servers().size());
  for (auto const& [id, entry] : cloud->servers()) {
    ordered.emplace_back(entry.priority, id);
  }
  std::sort(ordered.begin(), ordered.end());
  for (auto const& item : ordered) {
    peer_cloud_server_ids_.push_back(item.second);
  }

  // Same connection budget as Local Presence / peer PingCloudServers.
  dest_cloud_ = std::make_unique<CloudServerConnections>(
      ae_context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  work_cloud_ = dest_cloud_.get();
}

void QueryPeerPresence::OnCloud(Result<Cloud::ptr, int> result) {
  if (finished_) {
    return;
  }
  if (!result) {
    // Peer Personal Cloud unavailable — never fall back to observer cloud.
    used_observer_cloud_ = false;
    Complete(PeerPresence{PeerPresenceState::kUnknown});
    return;
  }
  BindPeerCloud(std::move(result).value());
  StartQuery();
}

void QueryPeerPresence::RefreshUsableSet() {
  samples_.clear();
  authoritative_server_ids_.clear();
  if (work_cloud_ == nullptr) {
    return;
  }

  // Local Presence contract = selected_servers() under RequestPolicy::All
  // (bounded by AE_CLOUD_MAX_SERVER_CONNECTIONS). Remote AND uses the same set.
  for (auto* sc : work_cloud_->selected_servers()) {
    if (sc == nullptr || !sc->server()) {
      continue;
    }
    authoritative_server_ids_.push_back(sc->server_id());
    RemoteServerPresenceSample sample{};
    sample.server_id = sc->server_id();
    if (sc->quarantine()) {
      sample.status = RemoteServerPresence::kExcluded;
    } else {
      sample.status = RemoteServerPresence::kUnknown;
    }
    samples_.push_back(sample);
  }

  AE_TELED_DEBUG(
      "REMOTE_PRESENCE peer_cloud_count={} authoritative_count={} "
      "selected_count={} max_connections={}",
      peer_cloud_server_ids_.size(), authoritative_server_ids_.size(),
      work_cloud_->selected_servers().size(), work_cloud_->max_connections());
}

void QueryPeerPresence::StartQuery() {
  if (finished_ || work_cloud_ == nullptr) {
    return;
  }
  RefreshUsableSet();
  std::size_t usable = 0;
  for (auto const& sample : samples_) {
    if (sample.status != RemoteServerPresence::kExcluded) {
      ++usable;
    }
  }
  if (usable == 0) {
    Complete(PeerPresence{PeerPresenceState::kUnknown});
    return;
  }

  quarantine_sub_ = work_cloud_->server_quarantined_event().Subscribe(
      [this](CloudServerConnection* sc) {
        if (finished_ || sc == nullptr) {
          return;
        }
        MarkExcluded(sc->server_id());
        MaybeComplete();
      });
  quarantine_release_sub_ =
      work_cloud_->server_quarantine_release_event().Subscribe(
          [this](CloudServerConnection* sc) {
            if (finished_ || sc == nullptr) {
              return;
            }
            OnServerRecovered(sc);
          });

  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc,
                               CloudRequest* request) {
        static_cast<void>(request);
        if (finished_ || sc == nullptr || !sc->server() || sc->quarantine()) {
          return;
        }
        auto const server_id = sc->server_id();
        for (auto const& sample : samples_) {
          if (sample.server_id == server_id &&
              (sample.status == RemoteServerPresence::kOnline ||
               sample.status == RemoteServerPresence::kOffline ||
               sample.status == RemoteServerPresence::kExcluded)) {
            return;
          }
        }
        if (std::find(queried_server_ids_.begin(), queried_server_ids_.end(),
                      server_id) == queried_server_ids_.end()) {
          queried_server_ids_.push_back(server_id);
        }
        auto& meta = attempts_[server_id];
        ++meta.generation;
        meta.send_time = Now();
        auto const generation = meta.generation;
        timing_subs_[server_id] =
            auth_api->get_client_timing(peer_uid_).Subscribe(
                [this, sc, generation](auto const& res) {
                  OnServerTiming(sc, generation, res);
                });
      }},
      *work_cloud_, RequestPolicy::All{},
      /*max_retries=*/kRemotePresenceQueryRetryCount + 1);

  exhausted_sub_ = cloud_request_->attempt_exhausted_event().Subscribe(
      [this](CloudServerConnection* sc) {
        if (finished_ || sc == nullptr) {
          return;
        }
        MarkUnknown(sc->server_id());
        MaybeComplete();
      });

  cloud_request_sub_ = cloud_request_->result_event().Subscribe([this](bool ok) {
    if (finished_) {
      return;
    }
    if (ok) {
      return;
    }
    MaybeComplete();
    if (!finished_ && AllUsableTerminal()) {
      Complete(AggregateRemotePresence(samples_));
    }
  });
}

void QueryPeerPresence::RequestTiming(CloudServerConnection* sc) {
  if (finished_ || sc == nullptr || !sc->server() || sc->quarantine()) {
    return;
  }
  auto* conn = sc->client_connection();
  if (conn == nullptr) {
    return;
  }
  auto const server_id = sc->server_id();
  for (auto const& sample : samples_) {
    if (sample.server_id == server_id &&
        (sample.status == RemoteServerPresence::kOnline ||
         sample.status == RemoteServerPresence::kOffline ||
         sample.status == RemoteServerPresence::kExcluded)) {
      return;
    }
  }

  if (std::find(queried_server_ids_.begin(), queried_server_ids_.end(),
                server_id) == queried_server_ids_.end()) {
    queried_server_ids_.push_back(server_id);
  }

  auto& meta = attempts_[server_id];
  ++meta.generation;
  meta.send_time = Now();
  auto const generation = meta.generation;

  // AuthorizedApiCall requires an active ApiContext path via CloudRequest's
  // handler; for recovered servers we re-enter through a one-server request.
  conn->AuthorizedApiCall(SubApi{[&, sc, generation](
                                     ApiContext<AuthorizedApi>& auth_api) {
    timing_subs_[server_id] = auth_api->get_client_timing(peer_uid_).Subscribe(
        [this, sc, generation](auto const& res) {
          OnServerTiming(sc, generation, res);
        });
  }});
}

void QueryPeerPresence::OnServerRecovered(CloudServerConnection* sc) {
  RemoteServerPresenceSample sample{};
  sample.server_id = sc->server_id();
  sample.status = RemoteServerPresence::kUnknown;
  bool found = false;
  for (auto& existing : samples_) {
    if (existing.server_id == sample.server_id) {
      existing = sample;
      found = true;
      break;
    }
  }
  if (!found) {
    samples_.push_back(sample);
    authoritative_server_ids_.push_back(sample.server_id);
  }
  // Fresh timing required — do not keep a stale ONLINE.
  RequestTiming(sc);
  MaybeComplete();
}

void QueryPeerPresence::OnServerTiming(
    CloudServerConnection* sc, std::uint64_t generation,
    Result<ClientTiming, std::int32_t> const& res) {
  if (finished_ || sc == nullptr) {
    return;
  }
  auto const server_id = sc->server_id();
  auto meta_it = attempts_.find(server_id);
  if (meta_it == attempts_.end() || meta_it->second.generation != generation) {
    return;
  }
  if (!res) {
    if (cloud_request_.has_value()) {
      auto const exhausted = cloud_request_->FailAttempt(sc);
      if (exhausted) {
        MarkUnknown(server_id);
        MaybeComplete();
      }
    } else {
      MarkUnknown(server_id);
      MaybeComplete();
    }
    return;
  }

  auto const recv = Now();
  TimePoint expected{};
  TimePoint deadline{};
  auto const status = ClassifyRemoteServerPresence(
      recv, meta_it->second.send_time, recv, res.value(), OfflineTimeout(),
      &expected, &deadline);

  for (auto& sample : samples_) {
    if (sample.server_id != server_id) {
      continue;
    }
    sample.status = status;
    sample.expected_open = expected;
    sample.offline_deadline = deadline;
    sample.next_ping_delta_ms = res.value().next_ping_delta_ms;
    sample.has_timing = true;
    break;
  }

  AE_TELED_DEBUG(
      "REMOTE_PRESENCE server {} next_delta {} status {} expected {} deadline {}",
      server_id, res.value().next_ping_delta_ms, static_cast<int>(status),
      expected, deadline);

  if (cloud_request_.has_value()) {
    cloud_request_->SucceedAttempt(sc);
  }
  MaybeComplete();
}

void QueryPeerPresence::MarkUnknown(ServerId server_id) {
  for (auto& sample : samples_) {
    if (sample.server_id == server_id &&
        sample.status != RemoteServerPresence::kExcluded &&
        sample.status != RemoteServerPresence::kOnline &&
        sample.status != RemoteServerPresence::kOffline) {
      sample.status = RemoteServerPresence::kUnknown;
      // Terminal unknown after retries — treat as observed for completion.
      sample.has_timing = true;
      return;
    }
  }
}

void QueryPeerPresence::MarkExcluded(ServerId server_id) {
  for (auto& sample : samples_) {
    if (sample.server_id == server_id) {
      sample.status = RemoteServerPresence::kExcluded;
      return;
    }
  }
}

bool QueryPeerPresence::AllUsableTerminal() const noexcept {
  for (auto const& sample : samples_) {
    if (sample.status == RemoteServerPresence::kExcluded) {
      continue;
    }
    if (!sample.has_timing &&
        sample.status == RemoteServerPresence::kUnknown) {
      return false;
    }
  }
  return true;
}

void QueryPeerPresence::MaybeComplete() {
  if (finished_) {
    return;
  }
  if (RemotePresenceCanEarlyCompleteOffline(samples_)) {
    Complete(PeerPresence{PeerPresenceState::kOffline});
    return;
  }
  if (RemotePresenceReadyForOnline(samples_)) {
    Complete(PeerPresence{PeerPresenceState::kOnline});
    return;
  }
  std::size_t usable = 0;
  for (auto const& sample : samples_) {
    if (sample.status != RemoteServerPresence::kExcluded) {
      ++usable;
    }
  }
  if (usable == 0) {
    Complete(PeerPresence{PeerPresenceState::kUnknown});
    return;
  }
  if (AllUsableTerminal()) {
    Complete(AggregateRemotePresence(samples_));
  }
}

void QueryPeerPresence::Complete(PeerPresence const& presence) {
  if (finished_) {
    return;
  }
  finished_ = true;
  timing_subs_.clear();
  exhausted_sub_.Reset();
  quarantine_sub_.Reset();
  quarantine_release_sub_.Reset();
  if (cloud_request_.has_value()) {
    cloud_request_->Succeeded();
  }
  result_event_.Emit(Ok{presence});
  Finish();
}

void QueryPeerPresence::Fail(int code) {
  if (finished_) {
    return;
  }
  finished_ = true;
  timing_subs_.clear();
  exhausted_sub_.Reset();
  quarantine_sub_.Reset();
  quarantine_release_sub_.Reset();
  if (cloud_request_.has_value()) {
    cloud_request_->Failed();
  }
  result_event_.Emit(Error{code});
  Finish();
}

}  // namespace ae

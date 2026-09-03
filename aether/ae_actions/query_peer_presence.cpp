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
  // Prefer peer Personal Cloud (cached or GetCloud). Fall back to the
  // observer's linked cloud only when peer cloud is unavailable — each
  // get_client_timing(uid) answer is still authoritative for that server.
  auto cached = client_->cloud_manager()->GetCachedCloud(peer_uid_);
  if (cached && cached.is_valid() && !cached->servers().empty()) {
    dest_cloud_ = std::make_unique<CloudServerConnections>(
        ae_context_, cached.Load(),
        client_->server_connection_manager().GetServerConnectionFactory(),
        AE_CLOUD_MAX_SERVER_CONNECTIONS);
    work_cloud_ = dest_cloud_.get();
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

void QueryPeerPresence::OnCloud(Result<Cloud::ptr, int> result) {
  if (finished_) {
    return;
  }
  if (!result) {
    // Peer cloud unavailable — fall back to observer cloud if it has usable
    // servers. get_client_timing remains per-server authoritative.
    auto& own = client_->cloud_connection();
    bool any_usable = false;
    for (auto* sc : own.selected_servers()) {
      if (sc != nullptr && sc->server() && !sc->quarantine()) {
        any_usable = true;
        break;
      }
    }
    if (!any_usable) {
      Complete(PeerPresence{PeerPresenceState::kUnknown});
      return;
    }
    work_cloud_ = &own;
    StartQuery();
    return;
  }
  auto cloud = std::move(result).value();
  dest_cloud_ = std::make_unique<CloudServerConnections>(
      ae_context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  work_cloud_ = dest_cloud_.get();
  StartQuery();
}

void QueryPeerPresence::RefreshUsableSet() {
  samples_.clear();
  if (work_cloud_ == nullptr) {
    return;
  }
  for (auto* sc : work_cloud_->selected_servers()) {
    if (sc == nullptr || !sc->server()) {
      continue;
    }
    RemoteServerPresenceSample sample{};
    sample.server_id = sc->server_id();
    if (sc->quarantine()) {
      sample.status = RemoteServerPresence::kExcluded;
    } else {
      sample.status = RemoteServerPresence::kUnknown;
    }
    samples_.push_back(sample);
  }
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

  quarantine_sub_ =
      work_cloud_->server_quarantined_event().Subscribe(
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
            // Recovered server re-enters usable set only after a new response.
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
            }
            MaybeComplete();
          });

  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc,
                               CloudRequest* request) {
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
        auto& meta = attempts_[server_id];
        ++meta.generation;
        meta.send_time = Now();
        auto const generation = meta.generation;
        timing_subs_[server_id] =
            auth_api->get_client_timing(peer_uid_).Subscribe(
                [this, sc, generation](auto const& res) {
                  OnServerTiming(sc, generation, res);
                });
        static_cast<void>(request);
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
    for (auto& sample : samples_) {
      if (sample.status == RemoteServerPresence::kUnknown &&
          !sample.has_timing) {
        // leave as Unknown contribution
      }
    }
    MaybeComplete();
    if (!finished_) {
      // All servers exhausted without Offline/Online completion.
      Complete(AggregateRemotePresence(samples_));
    }
  });
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
      "REMOTE_PRESENCE server {} next_delta {} status {}", server_id,
      res.value().next_ping_delta_ms, static_cast<int>(status));

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
  // All remaining usable samples known (Online/Unknown) and no Offline —
  // wait until every usable server has a terminal observation.
  bool any_pending = false;
  std::size_t usable = 0;
  for (auto const& sample : samples_) {
    if (sample.status == RemoteServerPresence::kExcluded) {
      continue;
    }
    ++usable;
    if (sample.status == RemoteServerPresence::kUnknown && !sample.has_timing) {
      // Still waiting unless retries exhausted left it Unknown without timing.
      auto it = attempts_.find(sample.server_id);
      if (it == attempts_.end()) {
        any_pending = true;
      }
    }
  }
  if (usable == 0) {
    Complete(PeerPresence{PeerPresenceState::kUnknown});
    return;
  }
  static_cast<void>(any_pending);
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

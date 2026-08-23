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

#include "aether/ae_actions/query_peer_receive_schedule.h"

#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/config.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/tele.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {

QueryPeerReceiveSchedule::QueryPeerReceiveSchedule(AeContext const& ae_context,
                                                   Client& client,
                                                   Uid peer_uid)
    : ae_context_{ae_context}, client_{&client}, peer_uid_{peer_uid} {
  auto& get_cloud = client_->cloud_manager()->GetCloud(peer_uid_);
  get_cloud_sub_ = get_cloud.result_event().Subscribe(
      [this](Result<Cloud::ptr, int> result) { OnCloud(std::move(result)); });
}

QueryPeerReceiveSchedule::~QueryPeerReceiveSchedule() {
  finished_ = true;
  query_state_.Cancel();
  timing_subs_.clear();
}

QueryPeerReceiveSchedule::ResultEvent::Subscriber
QueryPeerReceiveSchedule::result_event() noexcept {
  return EventSubscriber{result_event_};
}

Duration QueryPeerReceiveSchedule::OneWayEstimateFor(
    CloudServerConnection* sc) const {
  auto const fallback = FallbackOneWayPingEstimate();
  if (sc == nullptr) {
    return fallback;
  }
  auto* conn = sc->client_connection();
  if (conn == nullptr) {
    return fallback;
  }
  auto channel = conn->server_connection().current_channel();
  if (!channel) {
    return fallback;
  }
  auto const& stats = channel->channel_statistics().response_time_statistics();
  return OneWayPingEstimate(stats.empty(),
                            stats.empty() ? fallback : stats.min());
}

void QueryPeerReceiveSchedule::OnCloud(Result<Cloud::ptr, int> result) {
  if (!result) {
    Fail(static_cast<int>(QueryPeerReceiveScheduleError::kGetCloudFailed));
    return;
  }
  auto cloud = std::move(result).value();
  dest_cloud_ = std::make_unique<CloudServerConnections>(
      ae_context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  StartQuery();
}

void QueryPeerReceiveSchedule::StartQuery() {
  query_state_.Begin();
  timing_subs_.clear();

  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc,
                               CloudRequest* request) {
        if (finished_ || query_state_.cancelled) {
          return;
        }
        if (sc == nullptr || !sc->server()) {
          return;
        }
        auto const server_id = sc->server_id();
        auto existing = query_state_.attempts.find(server_id);
        if (existing != query_state_.attempts.end() &&
            existing->second.status == ServerTimingAttemptStatus::kSuccess) {
          return;
        }

        auto const qsend = Now();
        auto const one_way = OneWayEstimateFor(sc);
        auto const send_generation =
            query_state_.RegisterSend(server_id, qsend, one_way);

        timing_subs_[server_id] = auth_api->get_client_timing(peer_uid_).Subscribe(
            [this, sc, send_generation](auto const& res) {
              OnServerTiming(sc, send_generation, res);
            });
        static_cast<void>(request);
      }},
      *dest_cloud_, RequestPolicy::All{});

  cloud_request_sub_ =
      cloud_request_->result_event().Subscribe([this](bool ok) {
        if (finished_) {
          return;
        }
        if (ok) {
          return;
        }
        auto aggregated = query_state_.TryAggregate();
        if (aggregated.has_value()) {
          Complete(*aggregated);
          return;
        }
        Fail(static_cast<int>(
            QueryPeerReceiveScheduleError::kGetClientTimingFailed));
      });
}

void QueryPeerReceiveSchedule::OnServerTiming(
    CloudServerConnection* sc, std::uint64_t send_generation,
    Result<ClientTiming, int> const& res) {
  if (finished_ || sc == nullptr) {
    return;
  }
  auto const server_id = sc->server_id();
  if (!res) {
    if (!query_state_.ApplyError(server_id, send_generation)) {
      return;
    }
    if (cloud_request_.has_value()) {
      cloud_request_->FailAttempt(sc);
    }
    return;
  }
  if (!query_state_.ApplyTiming(server_id, send_generation, res.value())) {
    return;
  }
  auto const& timing = res.value();
  AE_TELED_DEBUG(
      "get_client_timing server {} next_delta_ms {} last_connect_delta_ms {}",
      server_id, timing.next_ping_delta_ms, timing.last_connect_delta_ms);
  MaybeComplete();
}

void QueryPeerReceiveSchedule::MaybeComplete() {
  if (finished_ || query_state_.cancelled) {
    return;
  }
  for (auto const& [id, attempt] : query_state_.attempts) {
    if (attempt.status == ServerTimingAttemptStatus::kInFlight) {
      return;
    }
  }
  auto aggregated = query_state_.TryAggregate();
  if (!aggregated.has_value()) {
    return;
  }
  Complete(*aggregated);
}

void QueryPeerReceiveSchedule::Complete(PeerReceiveSchedule const& schedule) {
  if (finished_) {
    return;
  }
  finished_ = true;
  query_state_.completed = true;
  ++query_state_.user_callback_count;
  timing_subs_.clear();
  if (cloud_request_.has_value()) {
    cloud_request_->Succeeded();
  }
  result_event_.Emit(Ok{schedule});
  Finish();
}

void QueryPeerReceiveSchedule::Fail(int code) {
  if (finished_) {
    return;
  }
  finished_ = true;
  query_state_.completed = true;
  ++query_state_.user_callback_count;
  timing_subs_.clear();
  result_event_.Emit(Error{code});
  Finish();
}

}  // namespace ae

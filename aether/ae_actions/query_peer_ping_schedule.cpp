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

#include "aether/ae_actions/query_peer_ping_schedule.h"

#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/config.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "aether/work_cloud_api/work_server_api/login_api.h"

namespace ae {

QueryPeerPingSchedule::QueryPeerPingSchedule(AeContext const& ae_context,
                                             Client& client, Uid peer_uid)
    : ae_context_{ae_context}, client_{&client}, peer_uid_{peer_uid} {
  auto& get_cloud = client_->cloud_manager()->GetCloud(peer_uid_);
  get_cloud_sub_ = get_cloud.result_event().Subscribe(
      [this](Result<Cloud::ptr, int>&& result) { OnCloud(std::move(result)); });
}

QueryPeerPingSchedule::ResultEvent::Subscriber
QueryPeerPingSchedule::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void QueryPeerPingSchedule::OnCloud(Result<Cloud::ptr, int>&& result) {
  if (!result) {
    Fail(static_cast<int>(QueryPeerPingScheduleError::kGetCloudFailed));
    return;
  }
  auto cloud = std::move(result).value();
  dest_cloud_ = std::make_unique<CloudServerConnections>(
      ae_context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  StartQuery();
}

void QueryPeerPingSchedule::BeginAttempt(ServerId server_id) {
  ++attempt_generation_;
  got_time_ = false;
  got_uap_ = false;
  attempt_failed_ = false;
  server_now_ms_ = 0;
  last_ping_ms_ = 0;
  delta_ms_ = 0;
  attempt_server_id_ = server_id;
  time_sub_.Reset();
  uap_sub_.Reset();
}

void QueryPeerPingSchedule::OnAttemptPromiseFailed(int error) {
  if (finished_ || attempt_failed_) {
    return;
  }
  attempt_failed_ = true;
  last_attempt_error_ = error;
  // Do not finish QueryPeerPingSchedule here: CloudRequest may retry on the
  // next channel/server. Terminal Fail() happens only when CloudRequest ends.
  if (attempt_request_ != nullptr) {
    attempt_request_->FailAttempt(attempt_sc_);
  }
}

void QueryPeerPingSchedule::StartQuery() {
  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc,
                               CloudRequest* request) {
        if (finished_) {
          return;
        }
        attempt_request_ = request;
        attempt_sc_ = sc;
        if (sc == nullptr || sc->server() == nullptr) {
          BeginAttempt(ServerId{});
          OnAttemptPromiseFailed(static_cast<int>(
              QueryPeerPingScheduleError::kMainServerUnavailable));
          return;
        }
        auto* conn = sc->client_connection();
        if (conn == nullptr) {
          BeginAttempt(sc->server()->server_id);
          OnAttemptPromiseFailed(static_cast<int>(
              QueryPeerPingScheduleError::kMainServerUnavailable));
          return;
        }

        BeginAttempt(sc->server()->server_id);
        auto const generation = attempt_generation_;

        conn->LoginApiCall(SubApi{[this, generation](
                                      ApiContext<LoginApi>& login) {
          time_sub_ = login->get_time_utc().Subscribe(
              [this, generation](auto const& res) {
                if (finished_ || generation != attempt_generation_) {
                  return;
                }
                if (attempt_failed_) {
                  return;
                }
                if (!res) {
                  OnAttemptPromiseFailed(static_cast<int>(
                      QueryPeerPingScheduleError::kGetTimeUtcFailed));
                  return;
                }
                server_now_ms_ = res.value();
                got_time_ = true;
                MaybeComplete(generation);
              });
        }});

        uap_sub_ = auth_api->get_uap(peer_uid_).Subscribe(
            [this, generation](auto const& res) {
              if (finished_ || generation != attempt_generation_) {
                return;
              }
              if (attempt_failed_) {
                return;
              }
              if (!res) {
                OnAttemptPromiseFailed(static_cast<int>(
                    QueryPeerPingScheduleError::kGetUapFailed));
                return;
              }
              auto const& uap = res.value();
              last_ping_ms_ = uap.last_read_timestamp_ms;
              delta_ms_ = uap.delta_ms;
              got_uap_ = true;
              MaybeComplete(generation);
            });
      }},
      *dest_cloud_, RequestPolicy::MainServer{});

  cloud_request_sub_ = cloud_request_->result_event().Subscribe([this](bool ok) {
    if (finished_) {
      return;
    }
    if (ok) {
      return;
    }
    Fail(last_attempt_error_ != 0
             ? last_attempt_error_
             : static_cast<int>(
                   QueryPeerPingScheduleError::kMainServerUnavailable));
  });
}

void QueryPeerPingSchedule::MaybeComplete(std::uint64_t generation) {
  if (finished_ || generation != attempt_generation_ || attempt_failed_) {
    return;
  }
  if (!got_time_ || !got_uap_) {
    return;
  }
  if (PeerPingScheduleValuesMalformed(server_now_ms_, last_ping_ms_)) {
    OnAttemptPromiseFailed(static_cast<int>(
        QueryPeerPingScheduleError::kMalformedResponse));
    return;
  }

  finished_ = true;
  auto schedule = MakePeerPingSchedule(
      server_now_ms_, last_ping_ms_, delta_ms_, attempt_server_id_,
      std::chrono::steady_clock::now());
  if (attempt_request_ != nullptr) {
    attempt_request_->Succeeded();
  }
  attempt_request_ = nullptr;
  attempt_sc_ = nullptr;
  result_event_.Emit(Ok{schedule});
  Finish();
}

void QueryPeerPingSchedule::Fail(int code) {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_event_.Emit(Error{code});
  Finish();
}

}  // namespace ae

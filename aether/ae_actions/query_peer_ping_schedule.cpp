/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
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

void QueryPeerPingSchedule::StartQuery() {
  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc,
                               CloudRequest* request) {
        if (sc == nullptr || sc->server() == nullptr) {
          Fail(static_cast<int>(
              QueryPeerPingScheduleError::kMainServerUnavailable));
          request->Failed();
          return;
        }
        auto* conn = sc->client_connection();
        if (conn == nullptr) {
          Fail(static_cast<int>(
              QueryPeerPingScheduleError::kMainServerUnavailable));
          request->Failed();
          return;
        }
        server_id_ = sc->server()->server_id;

        conn->LoginApiCall(SubApi{[this, request](ApiContext<LoginApi>& login) {
          time_sub_ = login->get_time_utc().Subscribe(
              [this, request](auto const& res) {
                if (!res) {
                  Fail(static_cast<int>(
                      QueryPeerPingScheduleError::kGetTimeUtcFailed));
                  request->Failed();
                  return;
                }
                server_now_ms_ = res.value();
                got_time_ = true;
                MaybeComplete(request);
              });
        }});

        uap_sub_ = auth_api->get_uap(peer_uid_).Subscribe(
            [this, request](auto const& res) {
              if (!res) {
                Fail(static_cast<int>(
                    QueryPeerPingScheduleError::kGetUapFailed));
                request->Failed();
                return;
              }
              auto const& uap = res.value();
              last_ping_ms_ = uap.last_read_timestamp_ms;
              delta_ms_ = uap.delta_ms;
              got_uap_ = true;
              MaybeComplete(request);
            });
      }},
      *dest_cloud_, RequestPolicy::MainServer{});

  cloud_request_sub_ = cloud_request_->result_event().Subscribe([this](bool ok) {
    if (!ok && !finished_) {
      Fail(static_cast<int>(
          QueryPeerPingScheduleError::kMainServerUnavailable));
    }
  });
}

void QueryPeerPingSchedule::MaybeComplete(CloudRequest* request) {
  if (finished_ || !got_time_ || !got_uap_) {
    return;
  }
  if (PeerPingScheduleValuesMalformed(server_now_ms_, last_ping_ms_)) {
    Fail(static_cast<int>(QueryPeerPingScheduleError::kMalformedResponse));
    request->Failed();
    return;
  }

  finished_ = true;
  auto schedule = MakePeerPingSchedule(
      server_now_ms_, last_ping_ms_, delta_ms_, server_id_,
      std::chrono::steady_clock::now());
  request->Succeeded();
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

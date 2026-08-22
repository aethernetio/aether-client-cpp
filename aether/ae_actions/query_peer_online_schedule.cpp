/*
 * Copyright 2026 Aethernet Inc.
 */

#include "aether/ae_actions/query_peer_online_schedule.h"

#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/config.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/server.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "aether/work_cloud_api/work_server_api/server_api_by_uid.h"

namespace ae {

QueryPeerOnlineSchedule::QueryPeerOnlineSchedule(AeContext const& ae_context,
                                                 Client& client, Uid peer_uid)
    : ae_context_{ae_context}, client_{&client}, peer_uid_{peer_uid} {
  auto& get_cloud = client_->cloud_manager()->GetCloud(peer_uid_);
  get_cloud_sub_ = get_cloud.result_event().Subscribe(
      [this](Result<Cloud::ptr, int>&& result) { OnCloud(std::move(result)); });
}

QueryPeerOnlineSchedule::ResultEvent::Subscriber
QueryPeerOnlineSchedule::result_event() noexcept {
  return EventSubscriber{result_event_};
}

void QueryPeerOnlineSchedule::OnCloud(Result<Cloud::ptr, int>&& result) {
  if (!result) {
    Fail(static_cast<int>(QueryPeerOnlineScheduleError::kGetCloudFailed));
    return;
  }
  auto cloud = std::move(result).value();
  dest_cloud_ = std::make_unique<CloudServerConnections>(
      ae_context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  StartQuery();
}

void QueryPeerOnlineSchedule::StartQuery() {
  cloud_request_.emplace(
      ae_context_,
      ApiRequestHandler{[this](ApiContext<AuthorizedApi>& auth_api,
                               CloudServerConnection* sc, CloudRequest* request) {
        if (sc != nullptr && sc->server()) {
          server_id_ = sc->server()->server_id;
        }
        auth_api->client(
            peer_uid_,
            SubApi<ServerApiByUid>{[this, request](
                                       ApiContext<ServerApiByUid>& api) {
              online_sub_ = api->online_time().Subscribe(
                  [this, request](auto const& res) {
                    if (!res) {
                      Fail(static_cast<int>(
                          QueryPeerOnlineScheduleError::kOnlineTimeFailed));
                      request->Failed();
                      return;
                    }
                    last_online_ms_ = res.value();
                    got_online_ = true;
                    MaybeComplete();
                    if (got_online_ && got_next_) {
                      request->Succeeded();
                    }
                  });
              next_sub_ = api->next_online_time().Subscribe(
                  [this, request](auto const& res) {
                    if (!res) {
                      // Old server / unsupported method 18.
                      Fail(static_cast<int>(QueryPeerOnlineScheduleError::
                                                kNextOnlineTimeUnsupported));
                      request->Failed();
                      return;
                    }
                    next_online_ms_ = res.value();
                    got_next_ = true;
                    MaybeComplete();
                    if (got_online_ && got_next_) {
                      request->Succeeded();
                    }
                  });
            }});
      }},
      *dest_cloud_, RequestPolicy::MainServer{});
}

void QueryPeerOnlineSchedule::MaybeComplete() {
  if (finished_ || !got_online_ || !got_next_) {
    return;
  }
  finished_ = true;
  PeerOnlineSchedule schedule{};
  schedule.last_online = ApiDateMillisToTimePoint(last_online_ms_);
  schedule.next_online = ApiDateMillisToOptional(next_online_ms_);
  schedule.server_id = server_id_;
  result_event_.Emit(Ok{schedule});
  Finish();
}

void QueryPeerOnlineSchedule::Fail(int code) {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_event_.Emit(Error{code});
  Finish();
}

}  // namespace ae

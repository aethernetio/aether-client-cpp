/*
 * Copyright 2026 Aethernet Inc.
 */

#ifndef AETHER_AE_ACTIONS_QUERY_PEER_ONLINE_SCHEDULE_H_
#define AETHER_AE_ACTIONS_QUERY_PEER_ONLINE_SCHEDULE_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "aether-miscpp/types/result.h"

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/events/events.h"
#include "aether/memory.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

namespace ae {
class Client;
class CloudServerConnections;

// Absolute server UTC timestamps. next_online is nullopt when the server
// returns Date(0) (unknown schedule).
struct PeerOnlineSchedule {
  std::chrono::system_clock::time_point last_online{};
  std::optional<std::chrono::system_clock::time_point> next_online;
  ServerId server_id{};
};

inline std::chrono::system_clock::time_point ApiDateMillisToTimePoint(
    std::int64_t millis) {
  return std::chrono::system_clock::time_point{
      std::chrono::milliseconds{millis}};
}

inline std::optional<std::chrono::system_clock::time_point>
ApiDateMillisToOptional(std::int64_t millis) {
  if (millis <= 0) {
    return std::nullopt;
  }
  return ApiDateMillisToTimePoint(millis);
}

// Queries TARGET peer MainServer:
//   GetCloud(peer) -> CloudServerConnections -> RequestPolicy::MainServer
//   -> AuthorizedApi.client(peer) -> ServerApiByUid.onlineTime + nextOnlineTime
enum class QueryPeerOnlineScheduleError : int {
  kGetCloudFailed = 1,
  kOnlineTimeFailed = 2,
  kNextOnlineTimeUnsupported = 3,
};

class QueryPeerOnlineSchedule final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerOnlineSchedule, int>)>;

  QueryPeerOnlineSchedule(AeContext const& ae_context, Client& client,
                          Uid peer_uid);

  AE_CLASS_NO_COPY_MOVE(QueryPeerOnlineSchedule)

  ResultEvent::Subscriber result_event() noexcept;

 private:
  void OnCloud(Result<Cloud::ptr, int>&& result);
  void StartQuery();
  void MaybeComplete();
  void Fail(int code);

  AeContext ae_context_;
  Client* client_{nullptr};
  Uid peer_uid_{};
  ResultEvent result_event_;
  Subscription get_cloud_sub_;
  std::unique_ptr<CloudServerConnections> dest_cloud_;
  std::optional<CloudRequest> cloud_request_;
  Subscription online_sub_;
  Subscription next_sub_;
  bool got_online_{false};
  bool got_next_{false};
  bool finished_{false};
  std::int64_t last_online_ms_{0};
  std::int64_t next_online_ms_{0};
  ServerId server_id_{};
};

}  // namespace ae

#endif  // AETHER_AE_ACTIONS_QUERY_PEER_ONLINE_SCHEDULE_H_

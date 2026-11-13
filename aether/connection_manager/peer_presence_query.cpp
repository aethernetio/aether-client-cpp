#include "aether/connection_manager/peer_presence_query.h"
#include <algorithm>
#include "aether/client.h"
#include "aether/server.h"
namespace ae {
PeerPresenceQuery::PeerPresenceQuery(AeContext const& context, Client& client,
                                     Uid peer)
    : Action{context},
      context_{context},
      client_{&client},
      peer_uid_{peer},
      result_event_{context},
      offline_timeout_{DefaultOfflineDetectionTimeout()} {
  if (auto policy = client.connectivity_policy().Load(); policy) {
    offline_timeout_ = policy->offline_detection_timeout();
    execution_policy_ = policy->cloud_request_execution_policy();
  }
  start_sub_ = context_.scheduler().Task([this]() {
    auto& action = client_->cloud_manager()->GetCloud(peer_uid_);
    cloud_sub_ = action.result_event().Subscribe(
        [this](auto result) { OnCloud(std::move(result)); });
  });
  deadline_sub_ = context_.scheduler().DelayedTask(
      [this]() { Complete({PeerPresenceState::kUnknown}); },
      std::chrono::seconds{20});
}
void PeerPresenceQuery::OnCloud(Result<Cloud::ptr, int> result) {
  if (completed_) {
    return;
  }
  if (!result) {
    Complete({PeerPresenceState::kUnknown});
    return;
  }
  auto cloud = std::move(result).value();
  std::vector<std::pair<std::uint16_t, ServerId>> ordered;
  for (auto const& [id, entry] : cloud->servers()) {
    ordered.emplace_back(entry.priority, id);
  }
  std::sort(ordered.begin(), ordered.end());
  for (auto const& [priority, id] : ordered) {
    static_cast<void>(priority);
    cloud_ids_.push_back(id);
  }
  cloud_ = std::make_unique<CloudServerConnections>(
      context_, cloud.Load(),
      client_->server_connection_manager().GetServerConnectionFactory(),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  for (auto* server : cloud_->selected_servers()) {
    auto const id = server->server_id();
    authoritative_ids_.push_back(id);
    samples_.push_back({id, server->quarantine()
                                ? RemoteServerPresence::kExcluded
                                : RemoteServerPresence::kUnknown});
    terminal_[id] = server->quarantine();
  }
  quarantine_sub_ =
      cloud_->server_quarantined_event().Subscribe([this](auto* server) {
        for (auto& sample : samples_) {
          if (sample.server_id == server->server_id()) {
            sample.status = RemoteServerPresence::kExcluded;
            terminal_[sample.server_id] = true;
          }
        }
        ScheduleCompletion();
      });
  for (auto* server : cloud_->selected_servers()) {
    if (server->quarantine()) {
      continue;
    }
    queried_ids_.push_back(server->server_id());
    auto action = std::make_unique<QueryPeerPresence>(
        context_, *server, peer_uid_, offline_timeout_, execution_policy_);
    query_subs_ += action->result_event().Subscribe([this](auto const& result) {
      for (auto& sample : samples_) {
        if (sample.server_id == result.server_id &&
            sample.status != RemoteServerPresence::kExcluded) {
          sample = result;
          terminal_[sample.server_id] = true;
        }
      }
      ScheduleCompletion();
    });
    queries_.push_back(std::move(action));
  }
  ScheduleCompletion();
}
void PeerPresenceQuery::ScheduleCompletion() {
  if (!completion_sub_ && !completed_) {
    completion_sub_ = context_.scheduler().Task([this]() {
      completion_sub_.Reset();
      MaybeComplete();
    });
  }
}
void PeerPresenceQuery::MaybeComplete() {
  if (completed_) {
    return;
  }
  bool terminal = true;
  for (auto& sample : samples_) {
    if (sample.status == RemoteServerPresence::kOnline &&
        Now() > sample.offline_deadline) {
      sample.status = RemoteServerPresence::kUnknown;
    }
    terminal = terminal && terminal_[sample.server_id];
  }
  if (RemotePresenceCanEarlyCompleteOffline(samples_) || terminal) {
    Complete(AggregateRemotePresence(samples_));
  }
}
void PeerPresenceQuery::Complete(PeerPresence result) {
  if (completed_) {
    return;
  }
  completed_ = true;
  start_sub_.Reset();
  completion_sub_.Reset();
  deadline_sub_.Reset();
  cloud_sub_.Reset();
  quarantine_sub_.Reset();
  query_subs_.Reset();
  queries_.clear();
  result_event_.Emit(Ok{result});
  Finish();
}
}  // namespace ae

#ifndef AETHER_CONNECTION_MANAGER_PEER_PRESENCE_QUERY_H_
#define AETHER_CONNECTION_MANAGER_PEER_PRESENCE_QUERY_H_
#include <map>
#include <memory>
#include <vector>
#include "aether/actions/action.h"
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/ae_context.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/remote_presence.h"
namespace ae {
class Client;
// Resolves the peer cloud and coordinates independent per-server actions.
class PeerPresenceQuery final : public Action {
 public:
  using ResultEvent = Event<void(Result<PeerPresence, int>)>;
  PeerPresenceQuery(AeContext const&, Client&, Uid);
  ResultEvent const& result_event() const { return result_event_; }
  Uid peer_uid() const { return peer_uid_; }
  auto const& samples() const { return samples_; }
  auto const& peer_cloud_server_ids() const { return cloud_ids_; }
  auto const& authoritative_server_ids() const { return authoritative_ids_; }
  auto const& queried_server_ids() const { return queried_ids_; }
  bool used_observer_cloud() const { return false; }

 private:
  void OnCloud(Result<Cloud::ptr, int>);
  void ScheduleCompletion();
  void MaybeComplete();
  void Complete(PeerPresence);
  AeContext context_;
  Client* client_;
  Uid peer_uid_;
  ResultEvent result_event_;
  Subscription cloud_sub_;
  MultiSubscription query_subs_;
  Subscription quarantine_sub_;
  TaskSubscription start_sub_;
  TaskSubscription completion_sub_;
  TaskSubscription deadline_sub_;
  std::unique_ptr<CloudServerConnections> cloud_;
  std::vector<std::unique_ptr<QueryPeerPresence>> queries_;
  std::vector<RemoteServerPresenceSample> samples_;
  std::vector<ServerId> cloud_ids_, authoritative_ids_, queried_ids_;
  std::map<ServerId, bool> terminal_;
  Duration offline_timeout_;
  CloudRequestExecutionPolicy execution_policy_;
  bool completed_{};
};
}  // namespace ae
#endif

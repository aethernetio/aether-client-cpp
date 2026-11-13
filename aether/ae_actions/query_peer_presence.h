#ifndef AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_
#define AETHER_AE_ACTIONS_QUERY_PEER_PRESENCE_H_
#include <map>
#include "aether/actions/action.h"
#include "aether/ae_context.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/remote_presence.h"
#include "aether/types/uid.h"
namespace ae {
// One resolved server only: no cloud lookup, restream or quarantine policy.
class QueryPeerPresence final : public Action {
 public:
  using ResultEvent = Event<void(RemoteServerPresenceSample const&)>;
  QueryPeerPresence(AeContext const&, CloudServerConnection&, Uid,
                    Duration offline_timeout, CloudRequestExecutionPolicy);
  ResultEvent const& result_event() const { return result_event_; }

 private:
  void StartAttempt();
  void OnTiming(std::uint16_t, Result<ClientTiming, std::int32_t> const&);
  void Complete(RemoteServerPresenceSample);
  AeContext context_;
  CloudServerConnection* server_;
  Uid peer_uid_;
  Duration offline_timeout_;
  CloudRequestExecutionPolicy policy_;
  CloudRequestServerExecState execution_;
  ResultEvent result_event_;
  MultiSubscription response_subs_, write_subs_;
  TaskSubscription task_, timeout_;
  std::map<std::uint16_t, TimePoint> send_times_;
  std::uint16_t generation_{};
  bool completed_{};
};
}  // namespace ae
#endif

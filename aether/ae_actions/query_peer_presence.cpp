#include "aether/ae_actions/query_peer_presence.h"
#include <algorithm>
#include "aether/api_protocol/api_protocol.h"
#include "aether/channels/channel.h"
#include "aether/diagnostics/presence_api_trace.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
namespace ae {
QueryPeerPresence::QueryPeerPresence(AeContext const& context,
                                     CloudServerConnection& server, Uid peer,
                                     Duration offline_timeout,
                                     CloudRequestExecutionPolicy policy)
    : Action{context},
      context_{context},
      server_{&server},
      peer_uid_{peer},
      offline_timeout_{offline_timeout},
      policy_{policy},
      result_event_{context} {
  NormalizeCloudRequestExecutionPolicy(policy_);
  execution_.activated = true;
  task_ = context_.scheduler().Task([this]() { StartAttempt(); });
}
void QueryPeerPresence::StartAttempt() {
  if (completed_) {
    return;
  }
  if (server_->quarantine()) {
    Complete({server_->server_id(), RemoteServerPresence::kExcluded});
    return;
  }
  auto* connection = server_->client_connection();
  if (!connection || !execution_.StartAttempt(policy_)) {
    Complete({server_->server_id(), RemoteServerPresence::kUnknown});
    return;
  }
  auto const generation = ++generation_;
  if (response_connection_ != connection) {
    response_connection_ = connection;
    first_send_time_ = Now();
  }
  // Rebind on every attempt: a replacement connection can reuse the old
  // allocation's address. Subscribe before Write, including synchronous pushes.
  response_sub_.Reset();
  response_sub_ = connection->client_safe_api().client_timing_event().Subscribe(
      [this](Uid const& uid, ClientTiming const& timing) {
        if (uid == peer_uid_) {
          OnTiming(timing);
        }
      });
  auto rtt = FallbackCloudRequestRtt();
  if (auto channel = connection->server_connection().current_channel()) {
    rtt = channel->channel_statistics()
              .response_time_statistics()
              .PercentileValue(policy_.response_percentile);
  }
  timeout_ = context_.scheduler().DelayedTask(
      [this]() {
        timeout_.Reset();
        auto action = execution_.OnSoftTimeout(policy_);
        if (action == CloudRequestServerExecState::SoftTimeoutAction::kRetry) {
          task_ = context_.scheduler().Task([this]() { StartAttempt(); });
        } else if (action ==
                   CloudRequestServerExecState::SoftTimeoutAction::kExhaust) {
          Complete({server_->server_id(), RemoteServerPresence::kUnknown});
        }
      },
      std::max(Duration{std::chrono::seconds{1}},
               ComputeCloudRequestSoftTimeout(rtt, policy_)));
  auto& write = connection->AuthorizedApiCall(
      SubApi{[this, generation](ApiContext<AuthorizedApi>& api) {
        api->GetClientTiming(peer_uid_);
        PresenceApiTrace("timing_sent", server_->server_id(), generation);
      }});
  if (completed_) {
    return;
  }
  write_subs_ +=
      write.status_event().Subscribe([this, generation](auto status) {
        PresenceApiTrace("timing_write", server_->server_id(), generation,
                         static_cast<int>(status));
        if (!completed_ && status == WriteAction::Status::kFail) {
          Complete({server_->server_id(), RemoteServerPresence::kUnknown});
        }
      });
}
void QueryPeerPresence::OnTiming(ClientTiming const& timing) {
  if (completed_) {
    return;
  }
  RemoteServerPresenceSample sample{};
  sample.server_id = server_->server_id();
  PresenceApiTrace("timing_received", server_->server_id(),
                   timing.next_ping_delta_ms, timing.last_connect_delta_ms);
  auto const now = Now();
  // Push replies echo only UID, not attempt identity. Never assign an old
  // response to a newer retry's send time; retain the first outstanding anchor.
  sample.status = ClassifyRemoteServerPresence(
      now, first_send_time_, now, timing, offline_timeout_,
      &sample.expected_open, &sample.offline_deadline);
  sample.next_ping_delta_ms = timing.next_ping_delta_ms;
  sample.has_timing = true;
  Complete(sample);
}
void QueryPeerPresence::Complete(RemoteServerPresenceSample sample) {
  if (completed_) {
    return;
  }
  completed_ = true;
  task_.Reset();
  timeout_.Reset();
  response_sub_.Reset();
  write_subs_.Reset();
  result_event_.Emit(sample);
  Finish();
}
}  // namespace ae

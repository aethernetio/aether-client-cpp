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
  send_times_[generation] = Now();
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
        auto promise = api->GetClientTiming(peer_uid_);
        PresenceApiTrace("timing_sent", server_->server_id(),
                         promise.request_id().id, generation);
        response_subs_ +=
            promise.Subscribe([this, generation](auto const& result) {
              OnTiming(generation, result);
            });
      }});
  write_subs_ += write.status_event().Subscribe([this, generation](auto status) {
    PresenceApiTrace("timing_write", server_->server_id(), generation,
                     static_cast<int>(status));
    if (!completed_ && status == WriteAction::Status::kFail) {
      Complete({server_->server_id(), RemoteServerPresence::kUnknown});
    }
  });
}
void QueryPeerPresence::OnTiming(
    std::uint16_t generation,
    Result<ClientTiming, std::int32_t> const& result) {
  if (completed_) {
    return;
  }
  auto it = send_times_.find(generation);
  if (it == send_times_.end()) {
    return;
  }
  RemoteServerPresenceSample sample{};
  sample.server_id = server_->server_id();
  if (result) {
    PresenceApiTrace("timing_received", server_->server_id(),
                     result.value().next_ping_delta_ms,
                     result.value().last_connect_delta_ms);
    auto const now = Now();
    sample.status = ClassifyRemoteServerPresence(
        now, it->second, now, result.value(), offline_timeout_,
        &sample.expected_open, &sample.offline_deadline);
    sample.next_ping_delta_ms = result.value().next_ping_delta_ms;
    sample.has_timing = true;
  } else {
    PresenceApiTrace("timing_error", server_->server_id(), generation,
                     result.error());
  }
  Complete(sample);
}
void QueryPeerPresence::Complete(RemoteServerPresenceSample sample) {
  if (completed_) {
    return;
  }
  completed_ = true;
  task_.Reset();
  timeout_.Reset();
  response_subs_.Reset();
  write_subs_.Reset();
  result_event_.Emit(sample);
  Finish();
}
}  // namespace ae

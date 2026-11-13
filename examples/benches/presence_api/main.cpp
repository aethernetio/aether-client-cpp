// Native Aether presence API only: no echo packets or inferred liveness.
#define AE_EXAMPLE_ETHERNET 1
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/all.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/connection_manager/peer_presence_query.h"
#include "aether_construct_ethernet.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace presence_api_internal {
using namespace std::chrono_literals;  // NOLINT: duration literals only.

std::int64_t QpcUs() {
  LARGE_INTEGER count{}, frequency{};
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&frequency);
  return static_cast<std::int64_t>(static_cast<long double>(count.QuadPart) *
                                   1000000 / frequency.QuadPart);
}

std::string Arg(int argc, char** argv, std::string_view key,
                std::string fallback = {}) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return argv[i + 1];
    }
  }
  return fallback;
}

class Trace {
 public:
  explicit Trace(std::string const& path) : file_{path} {
    file_ << "event,mono_us,a,b,c\n";
  }
  template <typename A = int, typename B = int, typename C = int>
  void Log(char const* event, A a = {}, B b = {}, C c = {}) {
    file_ << event << ',' << QpcUs() << ',' << a << ',' << b << ',' << c
          << '\n';
  }
  void Flush() { file_.flush(); }

 private:
  std::ofstream file_;
};

int StateCode(ae::PeerPresenceState state) {
  switch (state) {
    case ae::PeerPresenceState::kOnline:
      return 1;
    case ae::PeerPresenceState::kOffline:
      return 0;
    case ae::PeerPresenceState::kUnknown:
      return -1;
  }
  return -1;
}

struct Probe {
  explicit Probe(Trace& value) : trace{value} {}
  Trace& trace;
  ae::Client::ptr client;
  ae::Subscription selection;
  ae::Subscription query_subscription;
  ae::PeerPresenceQuery* query{};  // Owned exclusively by Client.
  std::optional<ae::Uid> peer;
  std::uint64_t query_id{};
  std::int64_t query_started{};
  bool query_done{false};
  bool query_overdue{false};
  bool selection_done{false};
  int local_previous{-1};
  bool announced{false};
  ae::MultiSubscription self_timing_subscriptions;
  std::vector<std::unique_ptr<ae::QueryPeerPresence>> self_timing_queries;

  void VerifyOwnAnnouncements(ae::AeContext const& context) {
    auto policy = client->connectivity_policy().Load();
    for (auto* server : client->cloud_connection().selected_servers()) {
      auto action = std::make_unique<ae::QueryPeerPresence>(
          context, *server, client->uid(), policy->offline_detection_timeout(),
          policy->cloud_request_execution_policy());
      self_timing_subscriptions +=
          action->result_event().Subscribe([this](auto const& sample) {
            trace.Log("self_server_timing", sample.server_id,
                      sample.next_ping_delta_ms, sample.has_timing);
          });
      self_timing_queries.push_back(std::move(action));
    }
  }

  bool AnnouncementsReady() {
    auto policy = client->connectivity_policy().Load();
    auto const servers = client->cloud_connection().selected_servers();
    if (servers.empty()) {
      return false;
    }
    for (auto* server : servers) {
      auto const* state = policy->FindServerPresence(server->server_id());
      auto* connection = server->client_connection();
      if (state == nullptr || !state->has_confirmed_schedule ||
          state->confirmed_interval != 1s || state->confirmed_rx_window != 1s ||
          state->config_change_pending || connection == nullptr ||
          connection->stream_info().link_state != ae::LinkState::kLinked ||
          !policy->IsServerLocallyOnline(server->server_id(), ae::Now())) {
        return false;
      }
    }
    for (auto* server : servers) {
      auto const* state = policy->FindServerPresence(server->server_id());
      trace.Log("announced_schedule", server->server_id(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    state->confirmed_ping_send_time.time_since_epoch())
                    .count(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    state->confirmed_pong_receive_time.time_since_epoch())
                    .count());
    }
    return true;
  }

  void Configure() {
    auto policy = client->connectivity_policy().Load();
    policy->ResetRxTimings();
    policy->SetOfflineDetectionTimeout(1s);
    policy->SetCloudRequestExecutionPolicy(
        ae::CloudRequestExecutionPolicy::FromFactor(
            ae::Percentile::FromPercent(99.99),
            ae::TimeoutFactor8::FromDouble(1.2), 2, 2));
    policy->ConfigureRxTimings(ae::RequestPolicy::All{})
        .ForAllPriorities(ae::RxTimingConf::Every(1s).WithWindow(1s));
    for (auto* server : client->cloud_connection().selected_servers()) {
      policy->ConfigureServerRxTiming(
          server->server_id(), ae::RxTimingConf::Every(1s).WithWindow(1s),
          ae::Percentile::FromPercent(99.0));
    }
    trace.Log("configuration", 1000, 1000, 1000);
  }

  void SampleLocal() {
    auto const online = client->IsLocallyOnline() ? 1 : 0;
    if (online != local_previous) {
      local_previous = online;
      trace.Log("local_state", online);
      auto diag = client->connectivity_policy().Load()->DiagnoseLocalPresence(
          ae::Now());
      trace.Log("local_schedule", diag.server_id,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    diag.expected_open.time_since_epoch())
                    .count(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    diag.offline_deadline.time_since_epoch())
                    .count());
      trace.Log("local_last_pong", diag.server_id,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    diag.last_pong.time_since_epoch())
                    .count());
    }
  }

  void QueryTick() {
    trace.Log("local_sample", client->IsLocallyOnline() ? 1 : 0);
    if (!peer) {
      return;
    }
    if (query != nullptr && !query_done && !query->is_finished()) {
      auto const age = QpcUs() - query_started;
      trace.Log("remote_query_busy", query_id, age);
      if (age >= 30000000 && !query_overdue) {
        query_overdue = true;
        trace.Log("remote_overdue", query_id, age);
      }
      return;
    }
    // Leave the result callback before replacing the Client-owned action:
    // QueryPeerPresence::Complete emits the result before calling Finish().
    query_subscription.Reset();
    query_done = false;
    query_overdue = false;
    ++query_id;
    query_started = QpcUs();
    trace.Log("remote_query_start", query_id);
    query = &client->QueryPeerPresence(*peer);
    query_subscription =
        query->result_event().Subscribe([this](auto const& result) {
          auto const elapsed = QpcUs() - query_started;
          if (result) {
            trace.Log("remote_sdk_state", query_id,
                      static_cast<int>(result.value().state));
            trace.Log("remote_result", query_id,
                      StateCode(result.value().state), elapsed);
          } else {
            trace.Log("remote_error", query_id, result.error(), elapsed);
          }
          for (auto const& sample : query->samples()) {
            trace.Log("remote_sample", query_id, sample.server_id,
                      static_cast<int>(sample.status));
            trace.Log("remote_sample_timing", sample.server_id,
                      sample.next_ping_delta_ms, sample.has_timing);
            trace.Log("remote_sample_window", sample.server_id,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          sample.expected_open.time_since_epoch())
                          .count(),
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          sample.offline_deadline.time_since_epoch())
                          .count());
          }
          for (auto sid : query->peer_cloud_server_ids()) {
            trace.Log("remote_cloud", query_id, sid);
          }
          for (auto sid : query->authoritative_server_ids()) {
            trace.Log("remote_authority", query_id, sid);
          }
          for (auto sid : query->queried_server_ids()) {
            trace.Log("remote_queried", query_id, sid);
          }
          trace.Log("remote_fallback", query_id, query->used_observer_cloud());
          query_done = true;
        });
    if (query->is_finished() && !query_done) {
      // A synchronous constructor completion cannot be recovered from an
      // event with no replay. Record missing observation, never invent a state.
      trace.Log("remote_unobserved", query_id, 0, QpcUs() - query_started);
      query_done = true;
    }
  }
};
}  // namespace presence_api_internal

int main(int argc, char** argv) {
  using namespace presence_api_internal;  // NOLINT: executable entry.
  auto const side = Arg(argc, argv, "--side", "A");
  auto const name = Arg(argc, argv, "--name", "native_presence_" + side);
  auto const uid_file = Arg(argc, argv, "--uid-file", "uid.txt");
  auto const peer_file = Arg(argc, argv, "--peer-file", "peer.txt");
  auto const stop_file = Arg(argc, argv, "--stop-file", "stop");
  auto const seconds = std::stoi(Arg(argc, argv, "--seconds", "3600"));
  Trace trace{Arg(argc, argv, "--trace", "events.csv")};
  trace.Log("start", side, 1000, 1000);
  trace.Flush();
  auto app = ae::examples::construct_aether_app();
  Probe probe{trace};
  probe.selection =
      app->aether()
          ->SelectClient(
              ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), name)
          .result_event()
          .Subscribe([&](auto const& result) {
            probe.selection_done = true;
            if (result) {
              probe.client = result.value();
              probe.Configure();
              trace.Log("selected");
            } else {
              trace.Log("registration_failed");
            }
            trace.Flush();
          });
  auto const begin = QpcUs();
  auto next_query = begin;
  auto next_flush = begin;
  auto previous = begin;
  while (!app->IsExited() &&
         QpcUs() - begin < static_cast<std::int64_t>(seconds) * 1000000) {
    auto const wake = app->Update(ae::Now());
    auto const now = QpcUs();
    if (std::filesystem::exists(stop_file)) {
      break;
    }
    if (now - previous > 50000) {
      trace.Log("loop_gap", now - previous);
    }
    previous = now;
    if (probe.selection_done && !probe.client) {
      return 2;
    }
    if (probe.client) {
      probe.SampleLocal();
      if (!probe.announced && probe.AnnouncementsReady()) {
        std::ofstream uid{uid_file};
        uid << ae::Format("{}", probe.client->uid());
        probe.announced = true;
        trace.Log("announcements_ready");
        probe.VerifyOwnAnnouncements(ae::AeContext{*app->aether()});
      }
      if (!probe.peer && std::filesystem::exists(peer_file)) {
        std::ifstream input{peer_file};
        std::string uid;
        input >> uid;
        if (uid.size() == 36) {
          probe.peer = ae::Uid::FromString(uid);
        }
      }
      if (now >= next_query) {
        probe.QueryTick();
        do {
          next_query += 1000000;
        } while (next_query <= now);
      }
    } else if (now - begin > 120000000) {
      trace.Log("registration_timeout");
      trace.Flush();
      return 3;
    }
    if (now >= next_flush) {
      trace.Flush();
      next_flush = now + 250000;
    }
    app->WaitUntil(std::min(wake, ae::Now() + 10ms));
  }
  if (probe.query != nullptr && !probe.query_done) {
    trace.Log("remote_censored", probe.query_id, QpcUs() - probe.query_started);
  }
  trace.Log("stop");
  trace.Flush();
  return probe.client ? 0 : 4;
}

// Aether-only two-process presence probe for QueryPeerPresence validation.
// Offline follows the first MissedDeadline quickly; Online waits until all
// relevant server observations complete with no Missed and >=1 Expected.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#if defined(RegisterClass)
#  undef RegisterClass
#endif

#define AE_EXAMPLE_ETHERNET 1
#include "aether/all.h"
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/receive_schedule.h"

#include "directory_domain_storage.h"

namespace {

using clock = std::chrono::steady_clock;

constexpr char const* kDefaultParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";

std::string_view ArgValue(int argc, char** argv, std::string_view key) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return argv[i + 1];
    }
  }
  return {};
}

bool HasFlag(int argc, char** argv, std::string_view key) {
  for (int i = 1; i < argc; ++i) {
    if (key == argv[i]) {
      return true;
    }
  }
  return false;
}

std::int64_t ArgI64(int argc, char** argv, std::string_view key,
                    std::int64_t fallback) {
  auto v = ArgValue(argc, argv, key);
  if (v.empty()) {
    return fallback;
  }
  return std::strtoll(v.data(), nullptr, 10);
}

char const* StateName(ae::PeerScheduleState state) {
  switch (state) {
    case ae::PeerScheduleState::kExpected:
      return "Expected";
    case ae::PeerScheduleState::kMissedDeadline:
      return "MissedDeadline";
    case ae::PeerScheduleState::kUnknown:
      return "Unknown";
  }
  return "?";
}

char const* PresenceName(ae::PeerPresenceState state) {
  switch (state) {
    case ae::PeerPresenceState::kOnline:
      return "Online";
    case ae::PeerPresenceState::kOffline:
      return "Offline";
    case ae::PeerPresenceState::kUnknown:
      return "Unknown";
  }
  return "?";
}

char const* AttemptStatusName(ae::ServerTimingAttemptStatus s) {
  switch (s) {
    case ae::ServerTimingAttemptStatus::kPending:
      return "Pending";
    case ae::ServerTimingAttemptStatus::kInFlight:
      return "InFlight";
    case ae::ServerTimingAttemptStatus::kRetrying:
      return "Retrying";
    case ae::ServerTimingAttemptStatus::kSuccess:
      return "Success";
    case ae::ServerTimingAttemptStatus::kTerminalError:
      return "TerminalError";
  }
  return "?";
}

bool OnlineFromPresence(ae::PeerPresenceState state) {
  return state == ae::PeerPresenceState::kOnline;
}

std::int64_t UtcUnixMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t SteadyMs(clock::time_point tp, clock::time_point origin) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(tp - origin)
      .count();
}

std::int64_t MsBetween(ae::TimePoint later, ae::TimePoint earlier) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier)
      .count();
}

std::unique_ptr<ae::AetherApp> MakeApp(std::filesystem::path const& state_dir) {
  auto state_dir_holder = std::make_shared<std::filesystem::path>(state_dir);
  return ae::AetherApp::Construct(
      ae::AetherAppContext{[state_dir_holder]() {
        return std::unique_ptr<ae::IDomainStorage>{
            std::make_unique<presence_probe::DirectoryDomainStorage>(
                *state_dir_holder)};
      }}
#if AE_DISTILLATION
          .AddAdapterFactory([](ae::AetherAppContext const& context) {
            return ae::EthernetAdapter::ptr::Create(
                ae::CreateWith{context.domain()}.with_id(
                    ae::GlobalId::kEthernetAdapter),
                context.aether(), context.poller(), context.dns_resolver());
          })
#endif
  );
}

std::int64_t Percentile(std::vector<std::int64_t> v, double p) {
  if (v.empty()) {
    return -1;
  }
  std::sort(v.begin(), v.end());
  auto const idx = static_cast<std::size_t>(
      std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1));
  return v[idx];
}

void PumpUntil(ae::AetherApp& app, clock::time_point until) {
  while (clock::now() < until && !app.IsExited()) {
    auto now = ae::Now();
    auto next = app.Update(now);
    app.WaitUntil(std::min(next, now + std::chrono::milliseconds{50}));
  }
}

struct QuerySample {
  std::uint32_t query_id{0};
  std::int64_t t_ms{0};
  std::int64_t utc_ms{0};
  std::int64_t query_start_utc_ms{0};
  std::int64_t query_latency_ms{0};
  bool success{false};
  int error{0};
  ae::PeerPresenceState state{ae::PeerPresenceState::kUnknown};
  bool online{false};
  std::int64_t last_online_age_ms{-1};
  std::int64_t ms_to_deadline{-1};
  bool had_deadline{false};
  std::size_t selected_servers{0};
  std::size_t queried_servers{0};
  std::size_t successful_servers{0};
  std::size_t failed_servers{0};
  std::size_t quarantined_skipped{0};
  std::size_t unresolved_servers{0};
  std::int64_t first_expected_delta_ms{-1};
  std::int64_t first_missed_delta_ms{-1};
  std::int64_t early_offline_delay_ms{-1};
  std::int64_t presence_online_after_last_server_ms{-1};
  std::int64_t early_online_delay_ms{-1};  // retained for CSV compatibility
};

}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  auto role = ArgValue(argc, argv, "--role");
  auto state_dir_s = ArgValue(argc, argv, "--state-dir");
  auto exchange_s = ArgValue(argc, argv, "--exchange-dir");
  if (role.empty() || state_dir_s.empty() || exchange_s.empty() ||
      HasFlag(argc, argv, "--help")) {
    std::cerr
        << "usage: aether_presence_probe --role observer|subject "
           "--state-dir DIR --exchange-dir DIR [--ping-ms N] [--window-ms N] "
           "[--duration-sec 60] [--queries-per-sec 3] [--csv PATH] "
           "[--jsonl PATH] [--server-diag]\n";
    return role.empty() ? 2 : 0;
  }

  bool const is_observer =
      (role == "observer" || role == "A" || role == "a" || role == "checker");
  bool const is_subject =
      (role == "subject" || role == "B" || role == "b" || role == "target");
  if (!is_observer && !is_subject) {
    std::cerr << "role must be observer|subject\n";
    return 2;
  }

  std::filesystem::path const state_dir{std::string{state_dir_s}};
  std::filesystem::path const exchange_dir{std::string{exchange_s}};
  std::filesystem::create_directories(state_dir);
  std::filesystem::create_directories(exchange_dir);

  auto parent_uid = std::string{ArgValue(argc, argv, "--parent-uid")};
  if (parent_uid.empty()) {
    parent_uid = kDefaultParentUid;
  }
  auto client_name = std::string{ArgValue(argc, argv, "--client-name")};
  if (client_name.empty()) {
    client_name = is_observer ? "presence-observer" : "presence-subject";
  }

  std::int64_t ping_ms =
      is_observer ? ArgI64(argc, argv, "--ping-ms", 60000)
                  : ArgI64(argc, argv, "--ping-ms", 3000);
  std::int64_t window_ms = ArgI64(argc, argv, "--window-ms", ping_ms);
  auto const duration_sec = ArgI64(argc, argv, "--duration-sec", 60);
  auto const queries_per_sec = ArgI64(argc, argv, "--queries-per-sec", 3);
  bool const server_diag = HasFlag(argc, argv, "--server-diag");

  auto csv_path = std::string{ArgValue(argc, argv, "--csv")};
  if (csv_path.empty()) {
    csv_path = (exchange_dir /
                (std::string{is_observer ? "observer" : "subject"} +
                 "_queries.csv"))
                   .string();
  }
  auto jsonl_path = std::string{ArgValue(argc, argv, "--jsonl")};
  if (jsonl_path.empty()) {
    jsonl_path = (exchange_dir /
                  (std::string{is_observer ? "observer" : "subject"} +
                   "_queries.jsonl"))
                     .string();
  }

  auto const my_uid_path =
      exchange_dir / (is_observer ? "observer_uid.txt" : "subject_uid.txt");
  auto const peer_uid_path =
      exchange_dir / (is_observer ? "subject_uid.txt" : "observer_uid.txt");

  std::cout << "role=" << (is_observer ? "observer" : "subject")
            << " SetReceiveSchedule ping_ms=" << ping_ms
            << " receive_window_ms=" << window_ms;
  if (is_observer) {
    std::cout << " queries_per_sec~=" << queries_per_sec
              << " server_diag=" << (server_diag ? 1 : 0);
  }
  std::cout << " duration_sec=" << duration_sec << std::endl;

  auto app = MakeApp(state_dir);
  ae::Client::ptr client;
  ae::Subscription select_sub;
  auto parent = ae::Uid::FromString(parent_uid);
  auto& select = app->aether()->SelectClient(parent, client_name);
  select_sub = select.result_event().Subscribe(
      [&](ae::Result<ae::Client::ptr, int> const& res) {
        if (!res) {
          std::cerr << "SelectClient failed code=" << res.error() << '\n';
          app->Exit(1);
          return;
        }
        client = res.value();
      });
  app->WaitActions(select);
  if (!client) {
    std::cerr << "client missing\n";
    return 3;
  }

  auto const schedule_ok = client->SetReceiveSchedule(ae::ReceiveSchedule{
      .ping_interval = std::chrono::duration_cast<ae::Duration>(
          std::chrono::milliseconds{ping_ms}),
      .receive_window = std::chrono::duration_cast<ae::Duration>(
          std::chrono::milliseconds{window_ms}),
  });
  if (!schedule_ok) {
    std::cerr << "SetReceiveSchedule failed code=" << schedule_ok.error()
              << '\n';
    return 4;
  }
  static_cast<void>(client->cloud_connection());
  app->aether().Save();

  std::string const local_uid = ae::Format("{}", client->uid());
  {
    std::ofstream out{my_uid_path, std::ios::out | std::ios::trunc};
    out << local_uid;
  }
  std::cout << "local_uid=" << local_uid << '\n';

  std::string peer_uid_text;
  auto const wait_peer_deadline = clock::now() + std::chrono::seconds{90};
  while (clock::now() < wait_peer_deadline) {
    if (std::filesystem::exists(peer_uid_path)) {
      std::ifstream in{peer_uid_path};
      std::getline(in, peer_uid_text);
      if (!peer_uid_text.empty()) {
        break;
      }
    }
    auto now = ae::Now();
    auto next = app->Update(now);
    app->WaitUntil(std::min(next, now + std::chrono::milliseconds{50}));
  }
  if (peer_uid_text.empty()) {
    std::cerr << "peer uid not found at " << peer_uid_path.string() << '\n';
    return 5;
  }
  std::cout << "peer_uid=" << peer_uid_text << '\n';

  PumpUntil(*app, clock::now() + std::chrono::seconds{5});

  if (is_subject) {
    auto const subject_start = clock::now();
    auto const subject_deadline =
        subject_start + std::chrono::seconds{duration_sec};
    std::int64_t last_log_ms = -1;
    while (clock::now() < subject_deadline && !app->IsExited()) {
      auto const now_tp = ae::Now();
      auto const utc = UtcUnixMs();
      if (last_log_ms < 0 || utc - last_log_ms >= 1000) {
        last_log_ms = utc;
        auto const last = client->last_online_time();
        auto const expected = client->expected_ping_response_time();
        std::cout << "SUBJECT_TICK utc_ms=" << utc
                  << " t_ms=" << SteadyMs(clock::now(), subject_start);
        if (last.has_value()) {
          std::cout << " last_online_age_ms=" << MsBetween(now_tp, *last);
        } else {
          std::cout << " last_online_age_ms=n/a";
        }
        if (expected.has_value()) {
          std::cout << " ms_to_expected_pong=" << MsBetween(*expected, now_tp);
        } else {
          std::cout << " ms_to_expected_pong=n/a";
        }
        std::cout << '\n';
      }
      auto now = ae::Now();
      auto next = app->Update(now);
      app->WaitUntil(std::min(next, now + std::chrono::milliseconds{50}));
    }
    std::cout << "\n=== SUMMARY role=subject ===\n";
    std::cout << "idle_online ping_ms=" << ping_ms
              << " window_ms=" << window_ms << " (status not queried)\n";
    return 0;
  }

  auto const peer_uid = ae::Uid::FromString(peer_uid_text);
  std::vector<QuerySample> samples;
  std::uint32_t online_count = 0;
  std::uint32_t offline_count = 0;
  std::uint32_t unknown_count = 0;
  std::uint32_t fails = 0;
  std::uint32_t transitions = 0;
  std::optional<bool> last_online;
  ae::Subscription query_sub;
  ae::Subscription schedule_sub;
  bool query_inflight = false;
  clock::time_point query_started{};
  std::int64_t query_start_utc_ms = 0;
  std::ofstream jsonl{jsonl_path, std::ios::out | std::ios::trunc};
  // Sampling window starts after optional server_diag so diag cannot eat the
  // entire --duration-sec budget.
  clock::time_point probe_start{};
  clock::time_point probe_deadline{};

  std::mt19937 rng{std::random_device{}()};
  double const mean_interval_ms =
      1000.0 / static_cast<double>(std::max<std::int64_t>(1, queries_per_sec));
  std::uniform_real_distribution<double> delay_dist(mean_interval_ms * 0.2,
                                                    mean_interval_ms * 1.8);
  auto schedule_next_query = [&]() {
    auto const delay_ms = static_cast<std::int64_t>(delay_dist(rng));
    return clock::now() + std::chrono::milliseconds{
                              std::max<std::int64_t>(20, delay_ms)};
  };
  clock::time_point next_query_at{};
  constexpr auto kQueryTimeout = std::chrono::seconds{15};

  // Client reuses an unfinished QueryPeerPresence for the same peer_uid.
  // Replacing with a throwaway uid destroys the hung action so sampling can
  // continue.
  auto force_drop_presence = [&](char const* reason) {
    auto const throwaway =
        ae::Uid::FromString("00000000-0000-0000-0000-000000000001");
    static_cast<void>(client->QueryPeerPresence(throwaway));
    query_sub.Reset();
    std::cout << "FORCE_DROP_PRESENCE reason=" << reason << '\n';
  };

  // Optional: server-scoped schedule diagnostics. Prefer observer cloud
  // selected_servers so we never leave a hung peer presence action behind.
  if (server_diag) {
    std::cout << "=== SERVER_DIAG begin (observer cloud servers) ===\n";
    std::vector<ae::ServerId> server_ids;
    for (auto* sc : client->cloud_connection().selected_servers()) {
      if (sc != nullptr && sc->server()) {
        server_ids.push_back(sc->server_id());
        std::cout << "SERVER_DIAG observer_cloud server_id=" << sc->server_id()
                  << '\n';
      }
    }
    if (server_ids.empty()) {
      std::cout << "SERVER_DIAG no observer servers yet; short presence probe\n";
      bool done = false;
      auto& discovery = client->QueryPeerPresence(peer_uid);
      ae::Subscription disc_sub = discovery.result_event().Subscribe(
          [&](ae::Result<ae::PeerPresence, int> const& res) {
            done = true;
            if (!res) {
              std::cout << "SERVER_DIAG presence FAIL err=" << res.error()
                        << '\n';
              return;
            }
            std::cout << "SERVER_DIAG presence="
                      << PresenceName(res.value().state) << '\n';
            for (auto const& d : discovery.server_diagnostics()) {
              server_ids.push_back(d.server_id);
              std::cout << "SERVER_DIAG discovered server_id=" << d.server_id
                        << " status=" << AttemptStatusName(d.status)
                        << " schedule="
                        << (d.has_raw ? StateName(d.converted.state) : "n/a")
                        << '\n';
            }
          });
      auto disc_deadline = clock::now() + std::chrono::seconds{20};
      while (!done && clock::now() < disc_deadline && !app->IsExited()) {
        auto now = ae::Now();
        auto next = app->Update(now);
        app->WaitUntil(std::min(next, now + std::chrono::milliseconds{20}));
      }
      if (!done) {
        force_drop_presence("server_diag_presence_timeout");
      } else {
        // Finished presence still occupies the slot until replaced; drop so
        // the sampling loop owns a fresh action.
        force_drop_presence("server_diag_presence_done");
      }
    }
    std::sort(server_ids.begin(), server_ids.end());
    server_ids.erase(std::unique(server_ids.begin(), server_ids.end()),
                     server_ids.end());
    for (auto const sid : server_ids) {
      bool sch_done = false;
      auto& sch = client->QueryPeerReceiveSchedule(peer_uid, sid);
      schedule_sub = sch.result_event().Subscribe(
          [&, sid](ae::Result<ae::PeerReceiveSchedule, int> const& res) {
            sch_done = true;
            if (!res) {
              std::cout << "SERVER_SCOPED server=" << sid
                        << " FAIL err=" << res.error() << '\n';
              jsonl << "{\"type\":\"server_scoped\",\"server_id\":" << sid
                    << ",\"success\":false,\"error\":" << res.error() << "}\n";
              return;
            }
            auto const& s = res.value();
            auto const now_tp = ae::Now();
            std::cout << "SERVER_SCOPED server=" << sid
                      << " state=" << StateName(s.state)
                      << " last_online_age_ms="
                      << MsBetween(now_tp, s.last_online)
                      << " ms_to_deadline="
                      << (s.next_ping_deadline
                              ? std::to_string(MsBetween(*s.next_ping_deadline,
                                                         now_tp))
                              : std::string{"n/a"})
                      << '\n';
            jsonl << "{\"type\":\"server_scoped\",\"server_id\":" << sid
                  << ",\"success\":true,\"state\":\"" << StateName(s.state)
                  << "\",\"last_online_age_ms\":"
                  << MsBetween(now_tp, s.last_online) << "}\n";
          });
      auto sch_deadline = clock::now() + std::chrono::seconds{20};
      while (!sch_done && clock::now() < sch_deadline && !app->IsExited()) {
        auto now = ae::Now();
        auto next = app->Update(now);
        app->WaitUntil(std::min(next, now + std::chrono::milliseconds{20}));
      }
      if (!sch_done) {
        std::cout << "SERVER_SCOPED server=" << sid << " TIMEOUT\n";
      }
    }
    std::cout << "=== SERVER_DIAG end ===\n";
    PumpUntil(*app, clock::now() + std::chrono::seconds{2});
  }

  probe_start = clock::now();
  probe_deadline = probe_start + std::chrono::seconds{duration_sec};
  next_query_at = schedule_next_query();

  auto finish_query = [&](ae::Result<ae::PeerPresence, int> const& res,
                          ae::QueryPeerPresence const& action) {
    auto const end = clock::now();
    QuerySample sample;
    sample.query_id = static_cast<std::uint32_t>(samples.size() + 1);
    sample.t_ms = SteadyMs(end, probe_start);
    sample.utc_ms = UtcUnixMs();
    sample.query_start_utc_ms = query_start_utc_ms;
    sample.query_latency_ms = SteadyMs(end, query_started);

    auto const cov = action.coverage();
    sample.selected_servers = cov.selected_server_count;
    sample.queried_servers = cov.queried_server_count;
    sample.successful_servers = cov.successful_server_count;
    sample.failed_servers = cov.failed_server_count;
    sample.quarantined_skipped = cov.quarantined_skipped_count;
    if (sample.queried_servers >=
        sample.successful_servers + sample.failed_servers) {
      sample.unresolved_servers = sample.queried_servers -
                                  sample.successful_servers -
                                  sample.failed_servers;
    }

    if (action.first_expected_time().has_value() &&
        action.completed_at().has_value()) {
      sample.early_online_delay_ms =
          MsBetween(*action.completed_at(), *action.first_expected_time());
      sample.first_expected_delta_ms =
          sample.query_latency_ms - sample.early_online_delay_ms;
    }
    if (action.first_missed_time().has_value() &&
        action.completed_at().has_value()) {
      sample.early_offline_delay_ms =
          MsBetween(*action.completed_at(), *action.first_missed_time());
      sample.first_missed_delta_ms =
          sample.query_latency_ms - sample.early_offline_delay_ms;
    }

    if (!res) {
      sample.success = false;
      sample.error = res.error();
      ++fails;
      std::cout << "Q#" << sample.query_id << " t=" << sample.t_ms
                << "ms FAIL err=" << sample.error
                << " latency=" << sample.query_latency_ms << "ms\n";
      jsonl << "{\"type\":\"presence\",\"query_id\":" << sample.query_id
            << ",\"query_start_utc_ms\":" << sample.query_start_utc_ms
            << ",\"callback_utc_ms\":" << sample.utc_ms
            << ",\"presence_latency_ms\":" << sample.query_latency_ms
            << ",\"success\":false,\"error\":" << sample.error
            << ",\"peer_uid\":\"" << peer_uid_text << "\"}\n";
    } else {
      sample.success = true;
      auto const& sch = res.value();
      sample.state = sch.state;
      sample.online = OnlineFromPresence(sch.state);
      if (action.last_server_completion_time().has_value() &&
          action.completed_at().has_value() &&
          sample.state == ae::PeerPresenceState::kOnline) {
        sample.presence_online_after_last_server_ms = MsBetween(
            *action.completed_at(), *action.last_server_completion_time());
      }
      auto const now_tp = ae::Now();
      if (sch.last_online.has_value()) {
        sample.last_online_age_ms = MsBetween(now_tp, *sch.last_online);
      }
      if (sch.next_ping_deadline.has_value()) {
        sample.had_deadline = true;
        sample.ms_to_deadline = MsBetween(*sch.next_ping_deadline, now_tp);
      }
      switch (sch.state) {
        case ae::PeerPresenceState::kOnline:
          ++online_count;
          break;
        case ae::PeerPresenceState::kOffline:
          ++offline_count;
          break;
        case ae::PeerPresenceState::kUnknown:
          ++unknown_count;
          break;
      }

      std::ostringstream servers_json;
      servers_json << '[';
      bool first = true;
      for (auto const& d : action.server_diagnostics()) {
        if (!first) {
          servers_json << ',';
        }
        first = false;
        servers_json << "{\"server_id\":" << d.server_id
                     << ",\"status\":\"" << AttemptStatusName(d.status) << "\"";
        if (d.has_raw) {
          servers_json << ",\"schedule\":\"" << StateName(d.converted.state)
                       << "\",\"next_ping_delta_ms\":"
                       << d.raw.next_ping_delta_ms
                       << ",\"last_connect_delta_ms\":"
                       << d.raw.last_connect_delta_ms;
          if (d.converted.next_ping_deadline.has_value()) {
            servers_json << ",\"ms_to_deadline\":"
                         << MsBetween(*d.converted.next_ping_deadline, now_tp);
          }
          servers_json << ",\"last_online_age_ms\":"
                       << MsBetween(now_tp, d.converted.last_online);
        } else if (d.status == ae::ServerTimingAttemptStatus::kTerminalError) {
          servers_json << ",\"schedule\":\"query_error\"";
        } else {
          servers_json << ",\"schedule\":\"unresolved\"";
        }
        servers_json << '}';
        std::cout << "  srv=" << d.server_id
                  << " status=" << AttemptStatusName(d.status);
        if (d.has_raw) {
          std::cout << " schedule=" << StateName(d.converted.state)
                    << " next_delta_ms=" << d.raw.next_ping_delta_ms
                    << " last_connect_delta_ms=" << d.raw.last_connect_delta_ms;
        }
        std::cout << '\n';
      }
      servers_json << ']';

      if ((sample.query_id % 25) == 0 || !sample.online ||
          sample.early_offline_delay_ms > 50) {
        std::cout << "Q#" << sample.query_id << " t=" << sample.t_ms
                  << "ms presence=" << PresenceName(sch.state)
                  << " latency=" << sample.query_latency_ms
                  << "ms early_offline_delay_ms="
                  << sample.early_offline_delay_ms
                  << " presence_online_after_last_server_ms="
                  << sample.presence_online_after_last_server_ms
                  << " coverage=" << sample.successful_servers << "/"
                  << sample.queried_servers
                  << " failed=" << sample.failed_servers
                  << " unresolved=" << sample.unresolved_servers << '\n';
      }

      jsonl << "{\"type\":\"presence\",\"query_id\":" << sample.query_id
            << ",\"query_start_utc_ms\":" << sample.query_start_utc_ms
            << ",\"callback_utc_ms\":" << sample.utc_ms
            << ",\"presence_latency_ms\":" << sample.query_latency_ms
            << ",\"success\":true,\"presence\":\"" << PresenceName(sch.state)
            << "\",\"online\":" << (sample.online ? "true" : "false")
            << ",\"first_expected_delta_ms\":" << sample.first_expected_delta_ms
            << ",\"first_missed_delta_ms\":" << sample.first_missed_delta_ms
            << ",\"early_offline_delay_ms\":" << sample.early_offline_delay_ms
            << ",\"presence_online_after_last_server_ms\":"
            << sample.presence_online_after_last_server_ms
            << ",\"early_online_delay_ms\":" << sample.early_online_delay_ms
            << ",\"last_online_age_ms\":" << sample.last_online_age_ms
            << ",\"ms_to_deadline\":" << sample.ms_to_deadline
            << ",\"coverage\":{\"selected\":" << sample.selected_servers
            << ",\"queried\":" << sample.queried_servers
            << ",\"successful\":" << sample.successful_servers
            << ",\"failed\":" << sample.failed_servers
            << ",\"quarantined_skipped\":" << sample.quarantined_skipped
            << ",\"unresolved\":" << sample.unresolved_servers << "}"
            << ",\"peer_uid\":\"" << peer_uid_text << "\""
            << ",\"servers\":" << servers_json.str() << "}\n";
    }

    if (last_online.has_value() && *last_online != sample.online) {
      ++transitions;
      std::cout << "  !! TRANSITION "
                << (*last_online ? "online" : "offline") << " -> "
                << (sample.online ? "online" : "offline")
                << " at t=" << sample.t_ms << "ms utc_ms=" << sample.utc_ms
                << " presence=" << PresenceName(sample.state) << '\n';
    }
    last_online = sample.online;
    samples.push_back(sample);
    query_inflight = false;
    next_query_at = schedule_next_query();
  };

  while (clock::now() < probe_deadline && !app->IsExited()) {
    auto const wall = clock::now();
    if (query_inflight && wall - query_started >= kQueryTimeout) {
      force_drop_presence("query_timeout");
      QuerySample sample;
      sample.query_id = static_cast<std::uint32_t>(samples.size() + 1);
      sample.t_ms = SteadyMs(wall, probe_start);
      sample.utc_ms = UtcUnixMs();
      sample.query_start_utc_ms = query_start_utc_ms;
      sample.query_latency_ms = SteadyMs(wall, query_started);
      sample.success = false;
      sample.error = -100;  // probe-local timeout
      ++fails;
      std::cout << "Q#" << sample.query_id << " t=" << sample.t_ms
                << "ms TIMEOUT after " << sample.query_latency_ms << "ms\n";
      jsonl << "{\"type\":\"presence\",\"query_id\":" << sample.query_id
            << ",\"query_start_utc_ms\":" << sample.query_start_utc_ms
            << ",\"callback_utc_ms\":" << sample.utc_ms
            << ",\"presence_latency_ms\":" << sample.query_latency_ms
            << ",\"success\":false,\"error\":-100,\"peer_uid\":\""
            << peer_uid_text << "\"}\n";
      samples.push_back(sample);
      query_inflight = false;
      next_query_at = schedule_next_query();
    }
    if (!query_inflight && wall >= next_query_at) {
      query_inflight = true;
      query_started = wall;
      query_start_utc_ms = UtcUnixMs();
      auto& action = client->QueryPeerPresence(peer_uid);
      query_sub = action.result_event().Subscribe(
          [&](ae::Result<ae::PeerPresence, int> const& res) {
            finish_query(res, action);
          });
    }
    auto now = ae::Now();
    auto next = app->Update(now);
    auto const wake_cap = now + std::chrono::milliseconds{20};
    if (next > wake_cap) {
      next = wake_cap;
    }
    app->WaitUntil(next);
  }

  auto drain_until = clock::now() + std::chrono::seconds{5};
  while (query_inflight && clock::now() < drain_until && !app->IsExited()) {
    auto now = ae::Now();
    auto next = app->Update(now);
    app->WaitUntil(std::min(next, now + std::chrono::milliseconds{20}));
  }

  {
    std::ofstream csv{csv_path, std::ios::out | std::ios::trunc};
    csv << "query_id,t_ms,utc_ms,query_start_utc_ms,query_latency_ms,success,"
           "error,presence,online,last_online_age_ms,ms_to_deadline,"
           "early_offline_delay_ms,first_missed_delta_ms,"
           "presence_online_after_last_server_ms,"
           "early_online_delay_ms,first_expected_delta_ms,selected,queried,"
           "successful,failed,quarantined_skipped,unresolved\n";
    for (auto const& s : samples) {
      csv << s.query_id << ',' << s.t_ms << ',' << s.utc_ms << ','
          << s.query_start_utc_ms << ',' << s.query_latency_ms << ','
          << (s.success ? 1 : 0) << ',' << s.error << ','
          << PresenceName(s.state) << ',' << (s.online ? 1 : 0) << ','
          << s.last_online_age_ms << ',' << s.ms_to_deadline << ','
          << s.early_offline_delay_ms << ',' << s.first_missed_delta_ms << ','
          << s.presence_online_after_last_server_ms << ','
          << s.early_online_delay_ms << ',' << s.first_expected_delta_ms << ','
          << s.selected_servers << ',' << s.queried_servers << ','
          << s.successful_servers << ',' << s.failed_servers << ','
          << s.quarantined_skipped << ',' << s.unresolved_servers << '\n';
    }
  }

  std::vector<std::int64_t> lats;
  std::vector<std::int64_t> early_offline_delays;
  std::vector<std::int64_t> online_after_last;
  for (auto const& s : samples) {
    if (s.success) {
      lats.push_back(s.query_latency_ms);
    }
    if (s.success && !s.online && s.early_offline_delay_ms >= 0) {
      early_offline_delays.push_back(s.early_offline_delay_ms);
    }
    if (s.success && s.online && s.presence_online_after_last_server_ms >= 0) {
      online_after_last.push_back(s.presence_online_after_last_server_ms);
    }
  }
  auto const ok = static_cast<std::uint32_t>(lats.size());
  double const online_pct =
      ok == 0 ? 0.0
              : (100.0 * static_cast<double>(online_count) /
                 static_cast<double>(ok));

  std::cout << "\n=== SUMMARY role=observer ===\n";
  std::cout << "observer ping/window=" << ping_ms << "/" << window_ms
            << "ms via QueryPeerPresence\n";
  std::cout << "queries=" << samples.size() << " ok=" << ok
            << " fail=" << fails << '\n';
  std::cout << "Online=" << online_count << " (" << online_pct
            << "%) Offline=" << offline_count
            << " Unknown=" << unknown_count << '\n';
  std::cout << "transitions(blinks)=" << transitions << '\n';
  std::cout << "latency_ms p50=" << Percentile(lats, 0.50)
            << " p90=" << Percentile(lats, 0.90)
            << " p99=" << Percentile(lats, 0.99);
  if (!lats.empty()) {
    std::cout << " max=" << *std::max_element(lats.begin(), lats.end());
  }
  std::cout << '\n';
  if (!early_offline_delays.empty()) {
    std::cout << "early_offline_delay_ms p50="
              << Percentile(early_offline_delays, 0.50)
              << " p90=" << Percentile(early_offline_delays, 0.90)
              << " max="
              << *std::max_element(early_offline_delays.begin(),
                                   early_offline_delays.end())
              << " n=" << early_offline_delays.size() << '\n';
  }
  if (!online_after_last.empty()) {
    std::cout << "presence_online_after_last_server_ms p50="
              << Percentile(online_after_last, 0.50)
              << " p90=" << Percentile(online_after_last, 0.90)
              << " max="
              << *std::max_element(online_after_last.begin(),
                                   online_after_last.end())
              << " n=" << online_after_last.size() << '\n';
  }
  std::cout << "csv=" << csv_path << '\n';
  std::cout << "jsonl=" << jsonl_path << '\n';
  return fails > ok ? 6 : 0;
}

/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "client_role.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/receive_schedule.h"

#include "common/deadline_ipc.h"
#include "common/directory_domain_storage.h"
#include "missed_deadline.h"

namespace ae::test_uap_peer_deadline {
namespace {

constexpr auto kBobPingInterval = std::chrono::milliseconds{3000};
constexpr auto kBobReceiveWindow = std::chrono::milliseconds{1000};
constexpr auto kQueryRetry = std::chrono::milliseconds{250};
constexpr auto kPastDeadlineMargin = std::chrono::milliseconds{500};
constexpr auto kAfterMissMargin = std::chrono::milliseconds{250};
constexpr auto kConfirmGap = std::chrono::milliseconds{1000};
constexpr auto kBeforeDeadlineLead = std::chrono::milliseconds{500};
constexpr auto kRecoveryTimeout = std::chrono::seconds{5};
constexpr auto kStabilizeTimeout = std::chrono::seconds{45};
constexpr auto kLiveCycleTimeout = std::chrono::seconds{20};

inline std::int64_t TimePointUs(TimePoint tp) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             tp.time_since_epoch())
      .count();
}

inline std::int64_t MsBetween(TimePoint later, TimePoint earlier) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier)
      .count();
}

inline void UidToHalves(Uid const& uid, std::int64_t& lo, std::int64_t& hi) {
  std::memcpy(&lo, uid.value.data(), 8);
  std::memcpy(&hi, uid.value.data() + 8, 8);
}

inline Uid UidFromHalves(std::int64_t lo, std::int64_t hi) {
  Uid uid{};
  std::memcpy(uid.value.data(), &lo, 8);
  std::memcpy(uid.value.data() + 8, &hi, 8);
  return uid;
}

struct QueryRow {
  std::string phase;
  std::uint32_t query_index{0};
  std::int64_t query_begin{0};
  std::int64_t query_end{0};
  std::int64_t last_online{0};
  std::int64_t next_ping_deadline{-1};
  std::int64_t now{0};
  int last_online_advanced{0};
  int deadline_passed{0};
  int query_success{0};
};

struct QueryOutcome {
  bool success{false};
  int error{0};
  PeerReceiveSchedule schedule{};
  TimePoint begin{};
  TimePoint end{};
  TimePoint now{};
};

enum class TestPhase {
  kIdle,
  kStabilize,
  kLive,
  kBeforeKill,
  kWaitKillAck,
  kBeforeDeadline,
  kAfterMiss,
  kAfterMissConfirm,
  kWaitRestartAck,
  kRecovery,
  kUnknown,
  kDone,
};

struct RoleState {
  Side side{};
  std::uint32_t run_id_hash{0};
  NamedPipeClient pipe;
  std::unique_ptr<AetherApp> app;
  Client::ptr client;
  Uid peer_uid{};
  bool peer_set{false};
  Subscription select_sub;
  Subscription query_sub;
  std::uint32_t ipc_seq{0};
  bool exit_requested{false};
  bool client_ready{false};
  bool cloud_started{false};
  bool test_running{false};
  TestPhase phase{TestPhase::kIdle};
  TimePoint test_start_{};
  TimePoint phase_deadline_{};
  TimePoint wait_until_{};
  TimePoint missed_deadline_{};
  TimePoint missed_anchor_last_online_{};
  TimePoint recovery_start_{};
  PeerReceiveSchedule previous_{};
  PeerReceiveSchedule p0_{};
  bool have_previous_{false};
  bool have_p0_{false};
  int live_cycle_{0};
  int advances_seen_{0};
  int false_missed_{0};
  std::uint32_t query_index_{0};
  bool query_inflight_{false};
  std::optional<QueryOutcome> last_query_{};
  std::vector<QueryRow> csv_rows_;
  std::string artifact_dir_;
  std::string fail_reason_;
  bool passed_{false};
  std::int64_t recovery_ms_{-1};
  std::int64_t ping_interval_ms{3000};
  std::int64_t deadline_late_by_ms_{-1};
  bool skip_before_deadline_{false};

  bool Emit(IpcType type, std::uint32_t code = 0, std::int64_t a = 0,
            std::int64_t b = 0, std::int64_t c = 0) {
    IpcFrame f{};
    f.type = static_cast<std::uint8_t>(type);
    f.side = static_cast<std::uint8_t>(side);
    f.run_id_hash = run_id_hash;
    f.seq = ++ipc_seq;
    f.code = code;
    f.local_us = TimePointUs(Now());
    f.a = a;
    f.b = b;
    f.c = c;
    return pipe.WriteFrame(f);
  }

  void Fail(std::string reason) {
    fail_reason_ = std::move(reason);
    passed_ = false;
    phase = TestPhase::kDone;
    Emit(IpcType::kTestDone, 1);
    exit_requested = true;
  }

  void Pass() {
    passed_ = true;
    phase = TestPhase::kDone;
    Emit(IpcType::kTestDone, 0, false_missed_, recovery_ms_,
         deadline_late_by_ms_);
    exit_requested = true;
  }

  void WriteCsv() {
    if (artifact_dir_.empty()) {
      return;
    }
    auto path = artifact_dir_ + "/queries.csv";
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    out << "phase,query_index,query_begin,query_end,last_online,"
           "next_ping_deadline,now,last_online_advanced,deadline_passed,"
           "query_success\n";
    auto const origin = TimePointUs(test_start_);
    for (auto const& r : csv_rows_) {
      out << r.phase << ',' << r.query_index << ',' << (r.query_begin - origin)
          << ',' << (r.query_end - origin) << ',' << (r.last_online - origin)
          << ','
          << (r.next_ping_deadline < 0 ? -1 : (r.next_ping_deadline - origin))
          << ',' << (r.now - origin) << ',' << r.last_online_advanced << ','
          << r.deadline_passed << ',' << r.query_success << '\n';
    }
  }

  void RecordCsv(std::string const& phase_name, QueryOutcome const& q,
                 PeerReceiveSchedule const* prev) {
    QueryRow row;
    row.phase = phase_name;
    row.query_index = query_index_;
    row.query_begin = TimePointUs(q.begin);
    row.query_end = TimePointUs(q.end);
    row.now = TimePointUs(q.now);
    row.query_success = q.success ? 1 : 0;
    if (q.success) {
      row.last_online = TimePointUs(q.schedule.last_online);
      row.next_ping_deadline =
          q.schedule.next_ping_deadline.has_value()
              ? TimePointUs(*q.schedule.next_ping_deadline)
              : -1;
      if (prev != nullptr) {
        row.last_online_advanced =
            IsLastOnlineAdvanced(prev->last_online, q.schedule.last_online) ? 1 : 0;
        if (prev->next_ping_deadline.has_value()) {
          row.deadline_passed =
              q.now > *prev->next_ping_deadline ? 1 : 0;
        }
      }
    }
    csv_rows_.push_back(row);
  }

  void BeginQuery() {
    if (!client || !peer_set || query_inflight_) {
      return;
    }
    query_inflight_ = true;
    last_query_.reset();
    auto const begin = Now();
    ++query_index_;
    query_sub.Reset();
    auto& action = client->QueryPeerReceiveSchedule(peer_uid);
    query_sub = action.result_event().Subscribe(
        [this, begin](Result<PeerReceiveSchedule, int> const& res) {
          QueryOutcome out;
          out.begin = begin;
          out.end = Now();
          out.now = out.end;
          if (!res) {
            out.success = false;
            out.error = res.error();
          } else {
            out.success = true;
            out.schedule = res.value();
          }
          last_query_ = out;
          query_inflight_ = false;
        });
  }

  bool WaitUntilAe(TimePoint until) {
    return Now() >= until;
  }

  void StartStabilize() {
    phase = TestPhase::kStabilize;
    phase_deadline_ = Now() + kStabilizeTimeout;
    have_previous_ = false;
    advances_seen_ = 0;
    wait_until_ = Now();
    BeginQuery();
  }

  void StartUnknown() {
    phase = TestPhase::kUnknown;
    phase_deadline_ = Now() + std::chrono::seconds{8};
    wait_until_ = Now();
    BeginQuery();
  }

  void OnStabilizeTick() {
    if (query_inflight_) {
      return;
    }
    if (last_query_.has_value()) {
      auto const& q = *last_query_;
      RecordCsv("stabilize", q, have_previous_ ? &previous_ : nullptr);
      if (!q.success) {
        wait_until_ = Now() + kQueryRetry;
        last_query_.reset();
        return;
      }
      // Do not apply MISSED_DEADLINE during stabilize: a slow query can finish
      // after the previous deadline without implying Bob is offline.
      if (!q.schedule.next_ping_deadline.has_value() ||
          !(*q.schedule.next_ping_deadline > q.now)) {
        wait_until_ = Now() + kQueryRetry;
        last_query_.reset();
        return;
      }
      if (have_previous_ &&
          IsLastOnlineAdvanced(previous_.last_online, q.schedule.last_online)) {
        ++advances_seen_;
      }
      previous_ = q.schedule;
      have_previous_ = true;
      // Need initial valid schedule + at least one advance (= 2 ping cycles).
      if (advances_seen_ >= 1) {
        p0_ = q.schedule;
        have_p0_ = true;
        auto const now = q.now;
        auto const age_ms = MsBetween(now, p0_.last_online);
        auto const until_ms = MsBetween(*p0_.next_ping_deadline, now);
        std::cout << "INITIAL last_online_age_ms=" << age_ms
                  << " until_next_ping_ms=" << until_ms << std::endl;
        std::cout << "## Live Bob\n"
                  << "cycle  last_online_advanced  until_next_ping_ms\n";
        std::cout << "0      yes                 " << until_ms << std::endl;
        live_cycle_ = 0;
        phase = TestPhase::kLive;
        wait_until_ = *p0_.next_ping_deadline + kPastDeadlineMargin;
        phase_deadline_ = wait_until_ + kLiveCycleTimeout;
        last_query_.reset();
        return;
      }
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (WaitUntilAe(wait_until_)) {
      if (Now() > phase_deadline_) {
        Fail("stabilize timeout");
        return;
      }
      BeginQuery();
    }
  }

  void OnLiveTick() {
    if (!WaitUntilAe(wait_until_)) {
      return;
    }
    if (query_inflight_) {
      return;
    }
    if (!last_query_.has_value()) {
      BeginQuery();
      return;
    }
    auto const& q = *last_query_;
    RecordCsv("live", q, &previous_);
    if (!q.success) {
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (IsMissedDeadline(previous_, q.schedule, q.now)) {
      // Only after we intentionally waited past the promised deadline.
      ++false_missed_;
      auto const prev_until =
          previous_.next_ping_deadline.has_value()
              ? MsBetween(*previous_.next_ping_deadline, q.now)
              : -1;
      std::cerr << "FALSE_MISSED_DEADLINE previous_last_online_rel_us="
                << (TimePointUs(previous_.last_online) - TimePointUs(test_start_))
                << " previous_deadline_rel_ms=" << prev_until
                << " query_last_online_rel_us="
                << (TimePointUs(q.schedule.last_online) - TimePointUs(test_start_))
                << " query_latency_ms=" << MsBetween(q.end, q.begin)
                << std::endl;
      Fail("FALSE_MISSED_DEADLINE");
      return;
    }
    if (!IsLastOnlineAdvanced(previous_.last_online, q.schedule.last_online) ||
        !q.schedule.next_ping_deadline.has_value() ||
        !(*q.schedule.next_ping_deadline > q.now)) {
      // Harness race: deadline+margin may still land before the server
      // publishes the new last_online. Retry until advance or false miss.
      if (Now() > phase_deadline_) {
        Fail("live last_online did not advance");
        return;
      }
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    ++live_cycle_;
    auto const until_ms = MsBetween(*q.schedule.next_ping_deadline, q.now);
    std::cout << live_cycle_ << "      yes                 " << until_ms
              << std::endl;
    previous_ = q.schedule;
    last_query_.reset();
    if (live_cycle_ >= 2) {
      // P0, P1, P2 => live_cycle_ 0 printed at stabilize, then 1 and 2 here.
      phase = TestPhase::kBeforeKill;
      wait_until_ = Now();
      phase_deadline_ = Now() + kLiveCycleTimeout;
      return;
    }
    wait_until_ = *q.schedule.next_ping_deadline + kPastDeadlineMargin;
    phase_deadline_ = wait_until_ + kLiveCycleTimeout;
  }

  void OnBeforeKillTick() {
    if (query_inflight_) {
      return;
    }
    if (!last_query_.has_value()) {
      BeginQuery();
      return;
    }
    auto const& q = *last_query_;
    RecordCsv("before_kill", q, &previous_);
    if (!q.success) {
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (!q.schedule.next_ping_deadline.has_value() ||
        !(*q.schedule.next_ping_deadline > q.now)) {
      Fail("BeforeKill missing future deadline");
      return;
    }
    // Need headroom so an in-flight Bob ping is unlikely to land after kill
    // and look like a post-deadline advance.
    auto const until_ms =
        MsBetween(*q.schedule.next_ping_deadline, q.now);
    if (until_ms < 1500) {
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      if (Now() > phase_deadline_) {
        Fail("BeforeKill could not obtain schedule with headroom");
      }
      return;
    }
    missed_anchor_last_online_ = q.schedule.last_online;
    missed_deadline_ = *q.schedule.next_ping_deadline;
    previous_ = q.schedule;
    last_query_.reset();
    phase = TestPhase::kWaitKillAck;
    Emit(IpcType::kRequestBobKill);
  }

  void OnBeforeDeadlineTick() {
    if (skip_before_deadline_) {
      phase = TestPhase::kAfterMiss;
      wait_until_ = missed_deadline_ + kAfterMissMargin;
      return;
    }
    auto const target = missed_deadline_ - kBeforeDeadlineLead;
    if (!WaitUntilAe(target)) {
      return;
    }
    if (Now() >= missed_deadline_) {
      skip_before_deadline_ = true;
      phase = TestPhase::kAfterMiss;
      wait_until_ = missed_deadline_ + kAfterMissMargin;
      return;
    }
    if (query_inflight_) {
      return;
    }
    if (!last_query_.has_value()) {
      BeginQuery();
      return;
    }
    auto const& q = *last_query_;
    RecordCsv("before_deadline", q, &previous_);
    if (!q.success) {
      // Skip phase rather than stall near deadline.
      phase = TestPhase::kAfterMiss;
      wait_until_ = missed_deadline_ + kAfterMissMargin;
      last_query_.reset();
      return;
    }
    // Still not offline; last_online may equal anchor.
    previous_ = q.schedule;
    last_query_.reset();
    phase = TestPhase::kAfterMiss;
    wait_until_ = missed_deadline_ + kAfterMissMargin;
  }

  void OnAfterMissTick() {
    if (!WaitUntilAe(wait_until_)) {
      return;
    }
    if (query_inflight_) {
      return;
    }
    if (!last_query_.has_value()) {
      BeginQuery();
      return;
    }
    auto const& q = *last_query_;
    RecordCsv("after_miss", q, &previous_);
    if (!q.success) {
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      if (Now() > missed_deadline_ + std::chrono::seconds{10}) {
        Fail("after_miss query timeout");
      }
      return;
    }
    if (!(q.now > missed_deadline_)) {
      wait_until_ = missed_deadline_ + kAfterMissMargin;
      last_query_.reset();
      return;
    }
    if (IsLastOnlineAdvanced(missed_anchor_last_online_, q.schedule.last_online)) {
      Fail("AfterMiss last_online advanced unexpectedly");
      return;
    }
    if (q.schedule.state == PeerScheduleState::kUnknown) {
      std::cerr << "AfterMiss aggregate Unknown (non-deterministic servers); "
                   "last_online frozen, continuing\n";
    } else if (!IsMissedDeadline(previous_, q.schedule, q.now)) {
      Fail("AfterMiss classification failed");
      return;
    }
    deadline_late_by_ms_ = MsBetween(q.now, missed_deadline_);
    previous_ = q.schedule;
    last_query_.reset();
    phase = TestPhase::kAfterMissConfirm;
    wait_until_ = Now() + kConfirmGap;
  }

  void OnAfterMissConfirmTick() {
    if (!WaitUntilAe(wait_until_)) {
      return;
    }
    if (query_inflight_) {
      return;
    }
    if (!last_query_.has_value()) {
      BeginQuery();
      return;
    }
    auto const& q = *last_query_;
    RecordCsv("after_miss_confirm", q, &previous_);
    if (!q.success) {
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (IsLastOnlineAdvanced(missed_anchor_last_online_, q.schedule.last_online)) {
      Fail("AfterMissConfirm last_online not frozen");
      return;
    }
    std::cout << "## Missed deadline\n"
              << "last_online_frozen: yes\n"
              << "deadline_passed_ms: " << deadline_late_by_ms_ << "\n"
              << "confirmation_after_ms: 1000\n";
    std::cout << "MISSED_DEADLINE deadline_late_by_ms=" << deadline_late_by_ms_
              << " last_online_unchanged=1" << std::endl;
    previous_ = q.schedule;
    last_query_.reset();
    phase = TestPhase::kWaitRestartAck;
    Emit(IpcType::kRequestBobRestart);
  }

  void OnRecoveryTick() {
    if (query_inflight_) {
      return;
    }
    if (last_query_.has_value()) {
      auto const& q = *last_query_;
      RecordCsv("recovery", q, &previous_);
      if (!q.success) {
        wait_until_ = Now() + kQueryRetry;
        last_query_.reset();
        return;
      }
      if (IsLastOnlineAdvanced(missed_anchor_last_online_, q.schedule.last_online) &&
          q.schedule.next_ping_deadline.has_value() &&
          *q.schedule.next_ping_deadline > q.now) {
        recovery_ms_ = MsBetween(q.now, recovery_start_);
        auto const until_ms =
            MsBetween(*q.schedule.next_ping_deadline, q.now);
        std::cout << "## Bob return\n"
                  << "last_online_advanced: yes\n"
                  << "recovery_ms: " << recovery_ms_ << "\n";
        std::cout << "RETURNED new_last_online=1 until_next_ping_ms=" << until_ms
                  << std::endl;
        WriteCsv();
        if (false_missed_ != 0) {
          Fail("false missed deadline count != 0");
          return;
        }
        std::cout << "UAP_PEER_DEADLINE_TEST PASS" << std::endl;
        Pass();
        return;
      }
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (Now() > recovery_start_ + kRecoveryTimeout) {
      Fail("recovery timeout");
      return;
    }
    if (WaitUntilAe(wait_until_)) {
      BeginQuery();
    }
  }

  void OnUnknownTick() {
    if (query_inflight_) {
      return;
    }
    if (last_query_.has_value()) {
      auto const& q = *last_query_;
      RecordCsv("unknown", q, have_previous_ ? &previous_ : nullptr);
      if (!q.success) {
        wait_until_ = Now() + kQueryRetry;
        last_query_.reset();
        return;
      }
      if (q.schedule.state == PeerScheduleState::kUnknown &&
          !q.schedule.next_ping_deadline.has_value()) {
        std::cout << "UNKNOWN_SCHEDULE state=Unknown next_ping_deadline=nullopt"
                  << std::endl;
        WriteCsv();
        std::cout << "UAP_PEER_DEADLINE_UNKNOWN PASS" << std::endl;
        Pass();
        return;
      }
      wait_until_ = Now() + kQueryRetry;
      last_query_.reset();
      return;
    }
    if (Now() > phase_deadline_) {
      Fail("unknown schedule timeout");
      return;
    }
    if (WaitUntilAe(wait_until_)) {
      BeginQuery();
    }
  }

  void TickTest() {
    if (!test_running || side != Side::kA) {
      return;
    }
    switch (phase) {
      case TestPhase::kStabilize:
        OnStabilizeTick();
        break;
      case TestPhase::kLive:
        OnLiveTick();
        break;
      case TestPhase::kBeforeKill:
        OnBeforeKillTick();
        break;
      case TestPhase::kWaitKillAck:
      case TestPhase::kWaitRestartAck:
        break;
      case TestPhase::kBeforeDeadline:
        OnBeforeDeadlineTick();
        break;
      case TestPhase::kAfterMiss:
        OnAfterMissTick();
        break;
      case TestPhase::kAfterMissConfirm:
        OnAfterMissConfirmTick();
        break;
      case TestPhase::kRecovery:
        OnRecoveryTick();
        break;
      case TestPhase::kUnknown:
        OnUnknownTick();
        break;
      default:
        break;
    }
  }

  void HandleIpc(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kSetPeerUid:
        peer_uid = UidFromHalves(f.a, f.b);
        peer_set = true;
        Emit(IpcType::kAck);
        break;
      case IpcType::kStartCloud:
        if (client) {
          (void)client->cloud_connection();
          cloud_started = true;
          Emit(IpcType::kCloudStarted);
        }
        break;
      case IpcType::kRunTest:
        if (side != Side::kA || !client || !peer_set) {
          Fail("Alice not ready for RunTest");
          return;
        }
        (void)client->cloud_connection();
        test_start_ = Now();
        test_running = true;
        if (f.code == 1) {
          StartUnknown();
        } else {
          StartStabilize();
        }
        break;
      case IpcType::kBobKilled:
        if (phase == TestPhase::kWaitKillAck) {
          auto const remaining = missed_deadline_ - Now();
          if (remaining > kBeforeDeadlineLead + std::chrono::milliseconds{50}) {
            phase = TestPhase::kBeforeDeadline;
            skip_before_deadline_ = false;
          } else {
            phase = TestPhase::kAfterMiss;
            wait_until_ = missed_deadline_ + kAfterMissMargin;
            skip_before_deadline_ = true;
          }
        }
        break;
      case IpcType::kBobRestarted:
        if (phase == TestPhase::kWaitRestartAck) {
          phase = TestPhase::kRecovery;
          recovery_start_ = Now();
          wait_until_ = Now();
        }
        break;
      case IpcType::kFlushState:
        // Persist domain only — no UAP graceful announce / setNextReadDelay(0).
        if (app && app->aether()) {
          app->aether().Save();
        }
        Emit(IpcType::kAck);
        break;
      case IpcType::kShutdown:
        exit_requested = true;
        Emit(IpcType::kAck);
        break;
      default:
        break;
    }
  }
};

std::unique_ptr<AetherApp> MakeApp(std::string const& state_dir) {
  return AetherApp::Construct(
      AetherAppContext{[state_dir]() {
        return std::unique_ptr<IDomainStorage>{
            std::make_unique<DirectoryDomainStorage>(state_dir)};
      }}
#if AE_DISTILLATION
          .AddAdapterFactory([](AetherAppContext const& context) {
            return EthernetAdapter::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    GlobalId::kEthernetAdapter),
                context.aether(), context.poller(), context.dns_resolver());
          })
#endif
  );
}

}  // namespace

int RunClientRole(ClientArgs const& args) {
  RoleState state;
  state.side = args.side;
  state.run_id_hash = HashRunId(args.run_id);
  state.artifact_dir_ = args.artifact_dir;

  if (!state.pipe.Connect(args.pipe_name, 60000)) {
    std::cerr << "pipe connect failed: " << args.pipe_name << "\n";
    return 2;
  }

  state.ping_interval_ms = args.ping_interval_ms;
  state.app = MakeApp(args.state_dir);
  auto parent = Uid::FromString(args.parent_uid);
  auto& select =
      state.app->aether()->SelectClient(parent, args.client_name);
  state.select_sub = select.result_event().Subscribe(
      [&](Result<Client::ptr, int> const& res) {
        if (!res) {
          state.Emit(IpcType::kTestDone, 2);
          state.exit_requested = true;
          return;
        }
        state.client = res.value();
        if (state.side == Side::kB) {
          auto ok = state.client->SetReceiveSchedule(ReceiveSchedule{
              .ping_interval = std::chrono::duration_cast<Duration>(
                  std::chrono::milliseconds{state.ping_interval_ms}),
              .receive_window =
                  std::chrono::duration_cast<Duration>(kBobReceiveWindow),
          });
          if (!ok) {
            state.Emit(IpcType::kTestDone, 3);
            state.exit_requested = true;
            return;
          }
        }
        state.client_ready = true;
        // Ensure clients_ map is on disk before a later hard-kill.
        state.app->aether().Save();
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        UidToHalves(state.client->uid(), lo, hi);
        state.Emit(IpcType::kUidReport, 0, lo, hi);
        state.Emit(IpcType::kChildReady);
      });

  while (!state.exit_requested && !state.app->IsExited()) {
    auto const now = Now();
    auto next = state.app->Update(now);
    if (auto frame = state.pipe.TryReadFrame(0)) {
      state.HandleIpc(*frame);
    }
    state.TickTest();
    state.app->WaitUntil(
        std::min(next, now + std::chrono::milliseconds{5}));
  }

  if (state.side == Side::kA && !state.fail_reason_.empty()) {
    std::cerr << "UAP_PEER_DEADLINE_TEST FAIL " << state.fail_reason_
              << std::endl;
    state.WriteCsv();
  }
  return state.passed_ ? 0 : (state.side == Side::kA ? 1 : 0);
}

}  // namespace ae::test_uap_peer_deadline

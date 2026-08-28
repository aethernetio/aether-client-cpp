    // Runtime acceptance: ping_retry_count=0 post-deadline same-cycle recovery.
    // Deterministic drop of Bob attempt #1; no pre-deadline retry before Tn.

    struct RczRetryRec {
      std::int64_t attempt_index{0};
      std::int64_t actual_send_time{0};
      double offset_from_Tn_ms{std::numeric_limits<double>::quiet_NaN()};
      std::int64_t logical_cycle_id{0};
    };

    struct RczAliceQuery {
      std::int64_t query_time{std::numeric_limits<std::int64_t>::min()};
      std::int64_t state{-2};
      std::int64_t last_online{std::numeric_limits<std::int64_t>::min()};
      std::int64_t next_deadline{0};
      bool valid{false};
    };

    struct RczCase {
      int case_index{0};
      std::string transport;
      std::int64_t seed{0};
      std::int64_t logical_cycle_id{0};
      std::int64_t tn_us{0};
      std::int64_t interval_ms{0};
      int ping_retry_count{0};
      double r99_ms{std::numeric_limits<double>::quiet_NaN()};
      double guard_ms{std::numeric_limits<double>::quiet_NaN()};
      std::int64_t first_attempt_send_time{0};
      std::int64_t first_attempt_index{0};
      int first_attempt_dropped{-1};
      int retry_attempt_count{0};
      std::vector<RczRetryRec> retries;
      std::int64_t first_post_deadline_retry_time{0};
      double first_post_deadline_retry_offset_ms{
          std::numeric_limits<double>::quiet_NaN()};
      std::int64_t confirmation_time{0};
      double confirmation_offset_from_Tn_ms{
          std::numeric_limits<double>::quiet_NaN()};
      std::int64_t confirming_attempt_index{0};
      std::int64_t next_nominal_deadline{0};
      std::int64_t expected_next_grid_deadline{0};
      double phase_error_ms{std::numeric_limits<double>::quiet_NaN()};
      int duplicate_count{0};
      RczAliceQuery alice_q1;
      RczAliceQuery alice_q2;
      bool fault_armed{false};
      bool fault_matched{false};
      bool fault_dropped{false};
      std::string harness_error;
      bool harness_valid{false};
      std::vector<std::string> production_failures;
      bool failed_pre_deadline_retry{false};
      bool failed_new_cycle_before_confirm{false};
      bool failed_duplicate{false};
    };


    auto abs_d = [](double v) { return v < 0 ? -v : v; };
    auto fmt3 = [](double v) -> std::string {
      if (!std::isfinite(v)) {
        return {};
      }
      std::ostringstream os;
      os << std::fixed << std::setprecision(3) << v;
      return os.str();
    };

    if (warmup_n <= 0) {
      std::cerr << "FAIL RETRY_COUNT_ZERO: no warm-up RTT samples\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }

    std::int64_t const period_us = args.ping_interval_ms * 1000;
    double const tick_ms = 1.0;
    int const n_cases = args.retry_count_zero_cases > 0
                            ? args.retry_count_zero_cases
                            : 10;
    int const ping_retry_count_cfg = static_cast<int>(kDefaultPingRetryCount);

    std::filesystem::create_directories(args.artifact_dir);
    std::ofstream samples_csv(std::filesystem::path{args.artifact_dir} /
                              "samples.csv");
    std::ofstream samples_jsonl(std::filesystem::path{args.artifact_dir} /
                                "samples.jsonl");
    std::ofstream failed_json(std::filesystem::path{args.artifact_dir} /
                              "failed-cases.json");
    std::ofstream report(std::filesystem::path{args.artifact_dir} /
                         "report.md");
    std::ofstream summary_json(std::filesystem::path{args.artifact_dir} /
                               "summary.json");
    if (!samples_csv || !samples_jsonl || !failed_json || !report ||
        !summary_json) {
      std::cerr << "FAIL cannot open retry-count-zero outputs\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }

    samples_csv
        << "case_index,transport,seed,logical_cycle_id,Tn_us,interval_ms,"
           "ping_retry_count,R99_ms,guard_ms,first_attempt_send_time,"
           "first_attempt_index,first_attempt_dropped,retry_attempt_count,"
           "first_post_deadline_retry_time,first_post_deadline_retry_offset_ms,"
           "confirmation_time,confirmation_offset_from_Tn_ms,"
           "confirming_attempt_index,next_nominal_deadline,"
           "expected_next_grid_deadline,phase_error_ms,duplicate_count,"
           "alice_q1_query_time,alice_q1_state,alice_q1_last_online,"
           "alice_q1_next_deadline,alice_q2_query_time,alice_q2_state,"
           "alice_q2_last_online,alice_q2_next_deadline,fault_armed,"
           "fault_matched,fault_dropped,harness_error,production_failures\n";

    using EvId = std::tuple<std::int64_t, std::uint8_t, std::int64_t,
                            std::int64_t, std::int64_t>;
    std::set<EvId> seen_ev;
    auto ev_id = [](BobPingEvent const& e) {
      return EvId{e.event_qpc, e.kind, e.logical_cycle_id,
                  e.physical_attempt_index, e.server_id};
    };
    for (auto const& e : bob.ping_events) {
      seen_ev.insert(ev_id(e));
    }
    auto take_ev = [&](BobPingEvent const& e) {
      seen_ev.insert(ev_id(e));
      return e;
    };
    auto dest_ok = [&](BobPingEvent const& e) {
      return dest == 0 || e.server_id == dest;
    };
    auto send_us = [](BobPingEvent const& e) -> std::int64_t {
      return e.actual_us != 0 ? e.actual_us : e.event_steady_us;
    };
    auto find_ev = [&](std::uint8_t kind, DWORD timeout_ms,
                       std::int64_t cycle_id, std::int64_t min_attempt)
        -> std::optional<BobPingEvent> {
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        for (auto const& e : bob.ping_events) {
          if (seen_ev.count(ev_id(e)) != 0 || !dest_ok(e)) {
            continue;
          }
          if (e.kind != kind) {
            continue;
          }
          if (cycle_id != 0 && e.logical_cycle_id != 0 &&
              e.logical_cycle_id != cycle_id) {
            continue;
          }
          if (min_attempt > 0 && e.physical_attempt_index < min_attempt) {
            continue;
          }
          return take_ev(e);
        }
      }
      return std::nullopt;
    };
    auto wait_sched = [&](DWORD timeout_ms) -> std::optional<ScheduleSnap> {
      auto const before = alice.schedules.size();
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        if (alice.schedules.size() > before) {
          return alice.schedules.back();
        }
      }
      return std::nullopt;
    };

    constexpr std::uint8_t kFaultTraceArmed = 1;
    constexpr std::uint8_t kFaultTraceMatched = 3;
    constexpr std::uint8_t kFaultTraceDropped = 4;

    std::int64_t last_settled_cycle_id = 0;
    for (auto const& e : bob.ping_events) {
      if (e.logical_cycle_id > last_settled_cycle_id) {
        last_settled_cycle_id = e.logical_cycle_id;
      }
    }

    auto wait_arm_ack = [&](DWORD timeout_ms) -> bool {
      bob.got_ack = false;
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        if (bob.got_ack) {
          return true;
        }
      }
      return false;
    };
    auto disarm_fault = [&]() -> bool {
      bob.got_ack = false;
      SendRaw(bob, kIpcArmFault, 0, dest, 1, 0, 0, 0, 0);
      return wait_arm_ack(3000);
    };
    auto arm_drop_next_first = [&]() -> bool {
      if (!disarm_fault()) {
        return false;
      }
      bob.got_ack = false;
      SendRaw(bob, kIpcArmFault, 0, dest, 1, 1, 0, 0, 0);
      return wait_arm_ack(3000);
    };

    struct DropWaitResult {
      std::optional<BobPingEvent> drop{};
      bool sent_instead{false};
      bool armed{false};
      bool matched{false};
      bool dropped_trace{false};
      std::int64_t arm_time{0};
      std::int64_t match_time{0};
      std::int64_t drop_time{0};
      std::int64_t cycle_id{0};
    };

    auto wait_intended_drop = [&](std::int64_t min_cycle_id,
                                  std::size_t trace_after, DWORD timeout_ms)
        -> DropWaitResult {
      DropWaitResult out{};
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        for (std::size_t i = trace_after; i < bob.fault_traces.size(); ++i) {
          auto const& t = bob.fault_traces[i];
          if (dest != 0 && t.server_id != dest) {
            continue;
          }
          if (t.kind == kFaultTraceArmed && !out.armed) {
            out.armed = true;
            out.arm_time = t.steady_us;
          }
          if (t.kind == kFaultTraceMatched && t.physical_attempt_index == 1) {
            if (out.drop.has_value()) {
              if (t.logical_cycle_id == out.drop->logical_cycle_id) {
                out.matched = true;
                out.match_time = t.steady_us;
              }
            } else if (t.logical_cycle_id > min_cycle_id) {
              out.matched = true;
              out.match_time = t.steady_us;
              out.cycle_id = t.logical_cycle_id;
            }
          }
          if (t.kind == kFaultTraceDropped && t.physical_attempt_index == 1) {
            if (out.drop.has_value()) {
              if (t.logical_cycle_id == out.drop->logical_cycle_id) {
                out.dropped_trace = true;
                out.drop_time = t.steady_us;
              }
            } else if (t.logical_cycle_id > min_cycle_id) {
              out.dropped_trace = true;
              out.drop_time = t.steady_us;
              out.cycle_id = t.logical_cycle_id;
            }
          }
        }
        for (auto const& e : bob.ping_events) {
          if (seen_ev.count(ev_id(e)) != 0 || !dest_ok(e)) {
            continue;
          }
          if (e.logical_cycle_id == 0 || e.logical_cycle_id <= min_cycle_id) {
            continue;
          }
          if (e.physical_attempt_index != 1) {
            continue;
          }
          if (e.kind ==
              static_cast<std::uint8_t>(PingTraceKind::kRequestSent)) {
            seen_ev.insert(ev_id(e));
            out.sent_instead = true;
            out.cycle_id = e.logical_cycle_id;
            return out;
          }
          if (e.kind ==
              static_cast<std::uint8_t>(PingTraceKind::kRequestDropped)) {
            out.drop = take_ev(e);
            out.cycle_id = e.logical_cycle_id;
          }
        }
        if (out.drop.has_value() && out.armed && out.matched &&
            out.dropped_trace) {
          return out;
        }
        if (out.sent_instead) {
          return out;
        }
      }
      return out;
    };

    auto wait_next_first_after = [&](std::int64_t after_cycle_id,
                                     DWORD timeout_ms)
        -> std::optional<BobPingEvent> {
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        for (auto const& e : bob.ping_events) {
          if (seen_ev.count(ev_id(e)) != 0 || !dest_ok(e)) {
            continue;
          }
          if (e.logical_cycle_id == 0 ||
              e.logical_cycle_id <= after_cycle_id) {
            continue;
          }
          if (e.physical_attempt_index != 1) {
            continue;
          }
          if (e.kind ==
                  static_cast<std::uint8_t>(PingTraceKind::kRequestSent) ||
              e.kind ==
                  static_cast<std::uint8_t>(PingTraceKind::kRequestDropped)) {
            return take_ev(e);
          }
        }
      }
      return std::nullopt;
    };

    auto refresh_fault_flags = [&](DropWaitResult& out,
                                   std::size_t trace_after) {
      if (!out.drop.has_value()) {
        return;
      }
      for (std::size_t i = trace_after; i < bob.fault_traces.size(); ++i) {
        auto const& t = bob.fault_traces[i];
        if (dest != 0 && t.server_id != dest) {
          continue;
        }
        if (t.kind == kFaultTraceArmed) {
          out.armed = true;
          out.arm_time = t.steady_us;
        }
        if (t.kind == kFaultTraceMatched &&
            t.physical_attempt_index == out.drop->physical_attempt_index &&
            t.logical_cycle_id == out.drop->logical_cycle_id) {
          out.matched = true;
          out.match_time = t.steady_us;
        }
        if (t.kind == kFaultTraceDropped &&
            t.physical_attempt_index == out.drop->physical_attempt_index &&
            t.logical_cycle_id == out.drop->logical_cycle_id) {
          out.dropped_trace = true;
          out.drop_time = t.steady_us;
        }
      }
    };

    auto expected_grid_after = [&](std::int64_t tn, std::int64_t ref_us) {
      for (int k = 1; k < 64; ++k) {
        auto const g = tn + static_cast<std::int64_t>(k) * period_us;
        if (g > ref_us) {
          return g;
        }
      }
      return tn + period_us;
    };

    auto prod_fail = [&](RczCase& c, char const* cls) {
      c.production_failures.push_back(cls);
      std::cerr << "FAIL case " << c.case_index << " " << cls << std::endl;
    };

    auto alice_query = [&](int checkpoint) -> std::optional<ScheduleSnap> {
      for (int try_i = 0; try_i < 6; ++try_i) {
        // force=1 so a leftover in-flight query still emits schedule state.
        SendRaw(alice, kIpcQueryNow, 0, checkpoint, 0, 1);
        if (auto aq = wait_sched(1500)) {
          return aq;
        }
      }
      return std::nullopt;
    };

    auto scan_cycle_events = [&](RczCase& c, bool confirmed) {
      int confirms = 0;
      bool saw_our_confirm = false;
      for (auto const& e : bob.ping_events) {
        if (!dest_ok(e)) {
          continue;
        }
        if (e.logical_cycle_id == c.logical_cycle_id &&
            e.kind ==
                static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed)) {
          saw_our_confirm = true;
          ++confirms;
        }
      }
      for (auto const& e : bob.ping_events) {
        if (!dest_ok(e)) {
          continue;
        }
        // New logical cycle before OUR confirmation is a production failure.
        // Ignore CycleStarted events that appear after we already confirmed.
        if (!confirmed && !saw_our_confirm &&
            e.logical_cycle_id > c.logical_cycle_id &&
            e.kind == static_cast<std::uint8_t>(PingTraceKind::kCycleStarted) &&
            !c.failed_new_cycle_before_confirm) {
          c.failed_new_cycle_before_confirm = true;
          prod_fail(c, "NEW_CYCLE_BEFORE_CONFIRM");
        }
        if (e.logical_cycle_id != c.logical_cycle_id) {
          continue;
        }
        if (e.physical_attempt_index < 2) {
          continue;
        }
        if (e.kind != static_cast<std::uint8_t>(PingTraceKind::kRequestSent) &&
            e.kind !=
                static_cast<std::uint8_t>(PingTraceKind::kRequestDropped)) {
          continue;
        }
        auto const t = send_us(e);
        if (c.tn_us != 0 && t < c.tn_us && !c.failed_pre_deadline_retry) {
          c.failed_pre_deadline_retry = true;
          prod_fail(c, "PRE_DEADLINE_RETRY");
        }
        bool already = false;
        for (auto const& r : c.retries) {
          if (r.attempt_index == e.physical_attempt_index &&
              r.actual_send_time == t) {
            already = true;
            break;
          }
        }
        if (already) {
          continue;
        }
        RczRetryRec rr{};
        rr.attempt_index = e.physical_attempt_index;
        rr.actual_send_time = t;
        rr.logical_cycle_id = e.logical_cycle_id;
        if (c.tn_us != 0) {
          rr.offset_from_Tn_ms =
              static_cast<double>(t - c.tn_us) / 1000.0;
        }
        c.retries.push_back(rr);
        if (c.tn_us != 0 && t >= c.tn_us &&
            c.first_post_deadline_retry_time == 0) {
          c.first_post_deadline_retry_time = t;
          c.first_post_deadline_retry_offset_ms = rr.offset_from_Tn_ms;
        }
      }
      if (confirms > 1 && !c.failed_duplicate) {
        c.failed_duplicate = true;
        c.duplicate_count += confirms - 1;
        prod_fail(c, "DUPLICATE_LOGICAL_PING");
      } else if (confirms > 1) {
        c.duplicate_count = confirms - 1;
      }
      c.retry_attempt_count = static_cast<int>(c.retries.size());
    };

    std::vector<RczCase> cases;
    int harness_armed_n = 0;
    int harness_matched_n = 0;
    int harness_dropped_n = 0;
    int harness_invalid_n = 0;
    int harness_wrong_request = 0;
    bool prod_ok = true;

    std::cout << "RETRY_COUNT_ZERO_RUNTIME cases=" << n_cases
              << " seed=" << args.seed << " transport=" << args.transport
              << " ping_retry_count=" << ping_retry_count_cfg << std::endl;

    wait_window_closed(8000);

    for (int ci = 0; ci < n_cases; ++ci) {
      RczCase rec{};
      rec.case_index = ci;
      rec.transport = args.transport;
      rec.seed = args.seed;
      rec.interval_ms = args.ping_interval_ms;
      rec.ping_retry_count = ping_retry_count_cfg;

      std::int64_t settle_before = last_settled_cycle_id;
      std::size_t fault_trace_before = bob.fault_traces.size();

      wait_window_closed(8000);

      bool armed_ok = false;
      DropWaitResult dw{};
      for (int arm_try = 0; arm_try < 4; ++arm_try) {
        armed_ok = arm_drop_next_first();
        if (armed_ok && arm_try == 0) {
          ++harness_armed_n;
        }
        dw = wait_intended_drop(settle_before, fault_trace_before, 8000);
        if (dw.drop.has_value()) {
          refresh_fault_flags(dw, fault_trace_before);
          drain(200);
          refresh_fault_flags(dw, fault_trace_before);
        }
        if (armed_ok && dw.drop.has_value() && dw.armed && dw.matched &&
            dw.dropped_trace && dw.drop->physical_attempt_index == 1 &&
            dw.drop->logical_cycle_id > settle_before) {
          break;
        }
        if (dw.sent_instead) {
          ++harness_wrong_request;
          if (dw.cycle_id > settle_before) {
            (void)find_ev(
                static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 3000,
                dw.cycle_id, 0);
            settle_before = dw.cycle_id;
            last_settled_cycle_id = dw.cycle_id;
          }
          disarm_fault();
          fault_trace_before = bob.fault_traces.size();
          dw = DropWaitResult{};
          continue;
        }
        disarm_fault();
        fault_trace_before = bob.fault_traces.size();
        dw = DropWaitResult{};
      }

      rec.fault_armed = armed_ok && dw.armed;
      rec.fault_matched = dw.matched;
      rec.fault_dropped = dw.dropped_trace;

      bool sync_ok = armed_ok && dw.drop.has_value() && dw.armed &&
                     dw.matched && dw.dropped_trace &&
                     dw.drop->physical_attempt_index == 1 &&
                     dw.drop->logical_cycle_id > settle_before;

      if (!sync_ok) {
        rec.harness_error = "HARNESS_FAULT_SYNC_ERROR";
        ++harness_invalid_n;
        prod_ok = false;
        if (dw.sent_instead) {
          rec.first_attempt_dropped = 0;
        }
        failed_json << "{\"case_index\":" << ci
                    << ",\"harness_error\":\"HARNESS_FAULT_SYNC_ERROR\"}\n";
        failed_json.flush();
        cases.push_back(std::move(rec));
        disarm_fault();
        continue;
      }

      rec.harness_valid = true;
      ++harness_matched_n;
      ++harness_dropped_n;

      BobPingEvent const first = *dw.drop;
      rec.logical_cycle_id = first.logical_cycle_id;
      rec.tn_us = first.cycle_anchor_us != 0 ? first.cycle_anchor_us
                                             : first.contract_deadline_us;
      rec.first_attempt_index = first.physical_attempt_index;
      rec.first_attempt_send_time = send_us(first);
      rec.first_attempt_dropped = 1;
      if (first.p99_rtt_us > 0) {
        rec.r99_ms = first.p99_rtt_us / 1000.0;
      }
      if (first.guard_us > 0) {
        rec.guard_ms = first.guard_us / 1000.0;
      }

      bool confirmed = false;
      bool q1_done = false;
      bool tn_passed = false;
      auto const case_deadline = GetTickCount64() + 20000;

      while (GetTickCount64() < case_deadline) {
        drain(20);
        scan_cycle_events(rec, confirmed);

        if (!confirmed) {
          for (auto const& e : bob.ping_events) {
            if (!dest_ok(e)) {
              continue;
            }
            if (e.logical_cycle_id != rec.logical_cycle_id) {
              continue;
            }
            if (e.kind !=
                static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed)) {
              continue;
            }
            confirmed = true;
            // Prefer ae-clock send time; CycleConfirmed may only have steady.
            rec.confirmation_time = send_us(e);
            if (rec.confirmation_time == 0 &&
                rec.first_post_deadline_retry_time != 0) {
              rec.confirmation_time = rec.first_post_deadline_retry_time;
            }
            rec.confirming_attempt_index = e.physical_attempt_index;
            auto const phase_ref =
                rec.confirmation_time != 0
                    ? rec.confirmation_time
                    : (rec.first_post_deadline_retry_time != 0
                           ? rec.first_post_deadline_retry_time
                           : rec.tn_us);
            if (rec.tn_us != 0 && phase_ref != 0) {
              rec.confirmation_offset_from_Tn_ms =
                  static_cast<double>(phase_ref - rec.tn_us) / 1000.0;
            }
            rec.next_nominal_deadline =
                e.contract_deadline_us != 0 ? e.contract_deadline_us
                                            : expected_grid_after(rec.tn_us,
                                                                  phase_ref);
            rec.expected_next_grid_deadline =
                expected_grid_after(rec.tn_us, phase_ref);
            rec.phase_error_ms =
                static_cast<double>(rec.next_nominal_deadline -
                                    rec.expected_next_grid_deadline) /
                1000.0;
            if (abs_d(rec.phase_error_ms) > tick_ms) {
              prod_fail(rec, "POST_RECOVERY_PHASE_DRIFT");
            }
            break;
          }
          if (confirmed) {
            break;
          }
        }

        if (rec.tn_us != 0 && first.event_qpc != 0 && first.actual_us != 0) {
          LARGE_INTEGER qfreq{};
          QueryPerformanceFrequency(&qfreq);
          double const qpc_per_us =
              static_cast<double>(qfreq.QuadPart) / 1000000.0;
          auto const tn_qpc =
              first.event_qpc +
              static_cast<std::int64_t>(
                  static_cast<double>(rec.tn_us - first.actual_us) *
                  qpc_per_us);
          auto now_qpc = QpcNow();
          if (now_qpc >= tn_qpc) {
            tn_passed = true;
          }
        } else if (rec.tn_us != 0) {
          for (auto const& e : bob.ping_events) {
            if (send_us(e) >= rec.tn_us) {
              tn_passed = true;
              break;
            }
          }
        }

        if (tn_passed && !q1_done && !confirmed) {
          q1_done = true;
          // Q1 is diagnostic only. Skip QueryPeer here so Q2 (required) is not
          // blocked by a stuck in-flight Alice query from the race window.
        }
      }

      scan_cycle_events(rec, confirmed);

      if (!confirmed) {
        prod_fail(rec, "NO_CONFIRMATION");
      }
      if (rec.first_post_deadline_retry_time == 0 && confirmed == false) {
        // still check post-deadline even if no confirm yet
      }
      bool has_post_deadline = false;
      for (auto const& r : rec.retries) {
        if (r.actual_send_time >= rec.tn_us) {
          has_post_deadline = true;
          break;
        }
      }
      if (!has_post_deadline) {
        prod_fail(rec, "NO_POST_DEADLINE_RETRY");
      }

      if (confirmed) {
        if (auto aq2 = alice_query(/*checkpoint*/ 42)) {
          rec.alice_q2.valid = true;
          rec.alice_q2.query_time = aq2->steady_us;
          rec.alice_q2.state = aq2->state;
          rec.alice_q2.last_online = aq2->last_online_us;
          rec.alice_q2.next_deadline = aq2->next_us;
          if (rec.alice_q2.state == 1) {
            prod_fail(rec, "ALICE_Q2_MISSED_DEADLINE");
          }
          if (rec.alice_q2.state == 0 && rec.alice_q2.next_deadline != 0 &&
              rec.tn_us != 0) {
            // next_deadline is ae-clock; snap to original grid (not query_time).
            auto const expected = expected_grid_after(
                rec.tn_us, rec.alice_q2.next_deadline - 1);
            double const alice_phase =
                static_cast<double>(rec.alice_q2.next_deadline - expected) /
                1000.0;
            if (abs_d(alice_phase) > tick_ms) {
              prod_fail(rec, "ALICE_Q2_PHASE_SHIFT");
            }
          }
        } else {
          rec.harness_error = "ALICE_Q2_QUERY_FAILED";
          std::cerr << "HARNESS case " << rec.case_index
                    << " ALICE_Q2_QUERY_FAILED (Bob recovery still valid)\n";
        }
        last_settled_cycle_id = rec.logical_cycle_id;
        // Let the next nominal cycle settle so the following arm is clean.
        disarm_fault();
        wait_window_closed(8000);
        auto next_first = wait_next_first_after(last_settled_cycle_id, 5000);
        if (next_first) {
          (void)find_ev(
              static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 5000,
              next_first->logical_cycle_id, 0);
          last_settled_cycle_id = next_first->logical_cycle_id;
        }
        wait_window_closed(8000);
      } else {
        disarm_fault();
        wait_window_closed(8000);
      }

      disarm_fault();

      if (!rec.production_failures.empty()) {
        prod_ok = false;
        failed_json << "{\"case_index\":" << ci
                    << ",\"logical_cycle_id\":" << rec.logical_cycle_id
                    << ",\"failures\":[";
        for (std::size_t fi = 0; fi < rec.production_failures.size(); ++fi) {
          if (fi != 0) {
            failed_json << ",";
          }
          failed_json << "\"" << rec.production_failures[fi] << "\"";
        }
        failed_json << "]}\n";
        failed_json.flush();
      }

      std::string prod_cls;
      for (std::size_t fi = 0; fi < rec.production_failures.size(); ++fi) {
        if (fi != 0) {
          prod_cls += ";";
        }
        prod_cls += rec.production_failures[fi];
      }

      samples_csv << rec.case_index << "," << rec.transport << "," << rec.seed
                  << "," << rec.logical_cycle_id << "," << rec.tn_us << ","
                  << rec.interval_ms << "," << rec.ping_retry_count << ","
                  << fmt3(rec.r99_ms) << "," << fmt3(rec.guard_ms) << ","
                  << rec.first_attempt_send_time << ","
                  << rec.first_attempt_index << ","
                  << rec.first_attempt_dropped << ","
                  << rec.retry_attempt_count << ","
                  << rec.first_post_deadline_retry_time << ","
                  << fmt3(rec.first_post_deadline_retry_offset_ms) << ","
                  << rec.confirmation_time << ","
                  << fmt3(rec.confirmation_offset_from_Tn_ms) << ","
                  << rec.confirming_attempt_index << ","
                  << rec.next_nominal_deadline << ","
                  << rec.expected_next_grid_deadline << ","
                  << fmt3(rec.phase_error_ms) << "," << rec.duplicate_count
                  << ",";
      if (rec.alice_q1.valid) {
        samples_csv << rec.alice_q1.query_time << "," << rec.alice_q1.state
                    << "," << rec.alice_q1.last_online << ","
                    << rec.alice_q1.next_deadline << ",";
      } else {
        samples_csv << ",,,,";
      }
      if (rec.alice_q2.valid) {
        samples_csv << rec.alice_q2.query_time << "," << rec.alice_q2.state
                    << "," << rec.alice_q2.last_online << ","
                    << rec.alice_q2.next_deadline << ",";
      } else {
        samples_csv << ",,,,";
      }
      samples_csv << (rec.fault_armed ? 1 : 0) << ","
                  << (rec.fault_matched ? 1 : 0) << ","
                  << (rec.fault_dropped ? 1 : 0) << ",\"" << rec.harness_error
                  << "\",\"" << prod_cls << "\"\n";
      samples_csv.flush();

      samples_jsonl << "{\"case_index\":" << rec.case_index
                    << ",\"transport\":\"" << rec.transport << "\""
                    << ",\"seed\":" << rec.seed
                    << ",\"logical_cycle_id\":" << rec.logical_cycle_id
                    << ",\"Tn_us\":" << rec.tn_us
                    << ",\"interval_ms\":" << rec.interval_ms
                    << ",\"ping_retry_count\":" << rec.ping_retry_count
                    << ",\"R99_ms\":" << fmt3(rec.r99_ms)
                    << ",\"guard_ms\":" << fmt3(rec.guard_ms)
                    << ",\"first_attempt_send_time\":"
                    << rec.first_attempt_send_time
                    << ",\"first_attempt_index\":" << rec.first_attempt_index
                    << ",\"first_attempt_dropped\":" << rec.first_attempt_dropped
                    << ",\"retry_attempt_count\":" << rec.retry_attempt_count
                    << ",\"retries\":[";
      for (std::size_t ri = 0; ri < rec.retries.size(); ++ri) {
        if (ri != 0) {
          samples_jsonl << ",";
        }
        auto const& r = rec.retries[ri];
        samples_jsonl << "{\"attempt_index\":" << r.attempt_index
                      << ",\"actual_send_time\":" << r.actual_send_time
                      << ",\"offset_from_Tn_ms\":" << fmt3(r.offset_from_Tn_ms)
                      << ",\"logical_cycle_id\":" << r.logical_cycle_id << "}";
      }
      samples_jsonl << "],\"first_post_deadline_retry_time\":"
                    << rec.first_post_deadline_retry_time
                    << ",\"first_post_deadline_retry_offset_ms\":"
                    << fmt3(rec.first_post_deadline_retry_offset_ms)
                    << ",\"confirmation_time\":" << rec.confirmation_time
                    << ",\"confirmation_offset_from_Tn_ms\":"
                    << fmt3(rec.confirmation_offset_from_Tn_ms)
                    << ",\"confirming_attempt_index\":"
                    << rec.confirming_attempt_index
                    << ",\"next_nominal_deadline\":"
                    << rec.next_nominal_deadline
                    << ",\"expected_next_grid_deadline\":"
                    << rec.expected_next_grid_deadline
                    << ",\"phase_error_ms\":" << fmt3(rec.phase_error_ms)
                    << ",\"duplicate_count\":" << rec.duplicate_count
                    << ",\"alice_q1\":{";
      if (rec.alice_q1.valid) {
        samples_jsonl << "\"query_time\":" << rec.alice_q1.query_time
                      << ",\"state\":" << rec.alice_q1.state
                      << ",\"last_online\":" << rec.alice_q1.last_online
                      << ",\"next_deadline\":" << rec.alice_q1.next_deadline;
      }
      samples_jsonl << "},\"alice_q2\":{";
      if (rec.alice_q2.valid) {
        samples_jsonl << "\"query_time\":" << rec.alice_q2.query_time
                      << ",\"state\":" << rec.alice_q2.state
                      << ",\"last_online\":" << rec.alice_q2.last_online
                      << ",\"next_deadline\":" << rec.alice_q2.next_deadline;
      }
      samples_jsonl << "},\"fault_armed\":" << (rec.fault_armed ? "true" : "false")
                    << ",\"fault_matched\":" << (rec.fault_matched ? "true" : "false")
                    << ",\"fault_dropped\":" << (rec.fault_dropped ? "true" : "false")
                    << ",\"harness_error\":\"" << rec.harness_error << "\""
                    << ",\"production_failures\":\"" << prod_cls << "\"}\n";
      samples_jsonl.flush();

      cases.push_back(std::move(rec));
    }

    auto collect_valid = [&](auto proj) {
      std::vector<double> v;
      for (auto const& c : cases) {
        if (!c.harness_valid) {
          continue;
        }
        double x = proj(c);
        if (std::isfinite(x)) {
          v.push_back(x);
        }
      }
      return v;
    };
    auto pct = [](std::vector<double> v, double p) {
      if (v.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      std::sort(v.begin(), v.end());
      double k = (v.size() - 1) * (p / 100.0);
      auto f = static_cast<std::size_t>(std::floor(k));
      auto c = static_cast<std::size_t>(std::ceil(k));
      if (f == c) {
        return v[f];
      }
      return v[f] * (c - k) + v[c] * (k - f);
    };
    auto dstat = [&](std::vector<double> const& v) {
      if (v.empty()) {
        return std::string("n=0");
      }
      auto s = v;
      std::sort(s.begin(), s.end());
      return "n=" + std::to_string(s.size()) + " min=" + fmt3(s.front()) +
             " p50=" + fmt3(pct(s, 50)) + " p95=" + fmt3(pct(s, 95)) +
             " max=" + fmt3(s.back());
    };

    int valid_n = 0;
    int fault_drop_ok = 0;
    int pre_deadline_retry_cases = 0;
    int post_deadline_recovery_cases = 0;
    int same_cycle_cases = 0;
    int new_cycle_cases = 0;
    int phase_error_nonzero = 0;
    int duplicate_total = 0;
    int duplicate_cases = 0;
    int production_fail_n = 0;
    int q1_valid = 0, q1_expected = 0, q1_missed = 0, q1_unknown = 0;
    int q2_valid = 0, q2_expected = 0, q2_missed = 0, q2_unknown = 0;
    int q2_phase_ok = 0;
    int q2_query_failed = 0;

    for (auto const& c : cases) {
      if (c.harness_valid) {
        ++valid_n;
      }
      if (c.harness_valid && c.first_attempt_dropped == 1) {
        ++fault_drop_ok;
      }
      bool pre = false;
      bool post = false;
      for (auto const& r : c.retries) {
        if (r.actual_send_time < c.tn_us) {
          pre = true;
        }
        if (r.actual_send_time >= c.tn_us) {
          post = true;
        }
      }
      if (pre) {
        ++pre_deadline_retry_cases;
      }
      if (post) {
        ++post_deadline_recovery_cases;
      }
      bool new_cycle = false;
      for (auto const& f : c.production_failures) {
        if (f == "NEW_CYCLE_BEFORE_CONFIRM") {
          new_cycle = true;
        }
      }
      if (c.harness_valid && !new_cycle && post) {
        ++same_cycle_cases;
      }
      if (new_cycle) {
        ++new_cycle_cases;
      }
      if (std::isfinite(c.phase_error_ms) && abs_d(c.phase_error_ms) > tick_ms) {
        ++phase_error_nonzero;
      }
      duplicate_total += c.duplicate_count;
      if (c.duplicate_count > 0) {
        ++duplicate_cases;
      }
      if (!c.production_failures.empty()) {
        ++production_fail_n;
      }
      if (c.alice_q1.valid) {
        ++q1_valid;
        if (c.alice_q1.state == 0) {
          ++q1_expected;
        } else if (c.alice_q1.state == 1) {
          ++q1_missed;
        } else {
          ++q1_unknown;
        }
      }
      if (c.harness_valid && c.harness_error == "ALICE_Q2_QUERY_FAILED") {
        ++q2_query_failed;
      }
      if (c.alice_q2.valid) {
        ++q2_valid;
        if (c.alice_q2.state == 0) {
          ++q2_expected;
          if (c.tn_us != 0 && c.alice_q2.next_deadline != 0) {
            auto const expected = expected_grid_after(
                c.tn_us, c.alice_q2.next_deadline - 1);
            double const e =
                static_cast<double>(c.alice_q2.next_deadline - expected) /
                1000.0;
            if (abs_d(e) <= tick_ms) {
              ++q2_phase_ok;
            }
          }
        } else if (c.alice_q2.state == 1) {
          ++q2_missed;
        } else {
          ++q2_unknown;
        }
      }
    }

    auto post_retry_offsets = collect_valid(
        [](RczCase const& c) { return c.first_post_deadline_retry_offset_ms; });
    auto attempts_until = collect_valid([&](RczCase const& c) {
      if (c.confirming_attempt_index <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      return static_cast<double>(c.confirming_attempt_index);
    });
    auto confirm_offsets =
        collect_valid([](RczCase const& c) {
          return c.confirmation_offset_from_Tn_ms;
        });
    auto phase_errors = collect_valid([](RczCase const& c) {
      return c.phase_error_ms;
    });

    report << "# ping_retry_count=0 runtime acceptance\n\n";
    report << "- transport: " << args.transport << "\n";
    report << "- seed: " << args.seed << "\n";
    report << "- planned: " << n_cases << "\n";
    report << "- valid: " << valid_n << "\n\n";
    report << "## Harness\n\n";
    report << "- fault correctly dropped: " << fault_drop_ok << "/" << valid_n
           << "\n";
    report << "- harness-invalid: " << harness_invalid_n << "\n";
    report << "- wrong-request drops: " << harness_wrong_request << "\n\n";
    report << "## Pre-deadline retries (target 0 cases)\n\n";
    report << "- cases with retry before Tn: " << pre_deadline_retry_cases
           << "\n\n";
    report << "## Post-deadline recovery\n\n";
    report << "- cases with retry after Tn: " << post_deadline_recovery_cases
           << "/" << valid_n << "\n";
    report << "- first post-deadline retry offset: "
           << dstat(post_retry_offsets) << "\n\n";
    report << "## Attempts until confirmation\n\n";
    report << "- " << dstat(attempts_until) << "\n\n";
    report << "## Confirmation offset from Tn\n\n";
    report << "- " << dstat(confirm_offsets) << "\n\n";
    report << "## Logical-cycle integrity\n\n";
    report << "- same-cycle recovery: " << same_cycle_cases << "/" << valid_n
           << "\n";
    report << "- unexpected new-cycle: " << new_cycle_cases << "\n\n";
    report << "## Phase\n\n";
    report << "- phase_error: " << dstat(phase_errors) << "\n";
    report << "- phase_error != 0: " << phase_error_nonzero << "\n\n";
    report << "## Duplicates\n\n";
    report << "- total: " << duplicate_total << "\n";
    report << "- cases with duplicates: " << duplicate_cases << "\n\n";
    report << "## Alice Q1 (diagnostic)\n\n";
    report << "- valid: " << q1_valid << " Expected: " << q1_expected
           << " MissedDeadline: " << q1_missed << " Unknown: " << q1_unknown
           << "\n\n";
    report << "## Alice Q2 (recovery check)\n\n";
    report << "- valid: " << q2_valid << " Expected: " << q2_expected
           << " MissedDeadline: " << q2_missed << " Unknown: " << q2_unknown
           << "\n";
    report << "- Q2 future deadline phase-correct: " << q2_phase_ok << "/"
           << q2_expected << "\n";
    report << "- Q2 query failed (harness): " << q2_query_failed << "\n\n";
    report << "## Failures\n\n";
    report << "- production failures: " << production_fail_n << "\n";
    report << "- harness failures: " << harness_invalid_n << "\n";
    report << "- alice Q2 query failures: " << q2_query_failed << "\n";

    summary_json << "{\n"
                 << "  \"transport\": \"" << args.transport << "\",\n"
                 << "  \"seed\": " << args.seed << ",\n"
                 << "  \"planned\": " << n_cases << ",\n"
                 << "  \"valid\": " << valid_n << ",\n"
                 << "  \"harness\": {\n"
                 << "    \"fault_correctly_dropped\": " << fault_drop_ok
                 << ",\n"
                 << "    \"harness_invalid\": " << harness_invalid_n << ",\n"
                 << "    \"alice_q2_query_failed\": " << q2_query_failed << "\n"
                 << "  },\n"
                 << "  \"pre_deadline_retry_cases\": "
                 << pre_deadline_retry_cases << ",\n"
                 << "  \"post_deadline_recovery_cases\": "
                 << post_deadline_recovery_cases << ",\n"
                 << "  \"same_cycle_recovery_cases\": " << same_cycle_cases
                 << ",\n"
                 << "  \"unexpected_new_cycle_cases\": " << new_cycle_cases
                 << ",\n"
                 << "  \"phase_error_nonzero_count\": " << phase_error_nonzero
                 << ",\n"
                 << "  \"duplicate_total\": " << duplicate_total << ",\n"
                 << "  \"production_failures\": " << production_fail_n << ",\n"
                 << "  \"harness_failures\": " << harness_invalid_n << ",\n"
                 << "  \"alice_q1\": {\"valid\": " << q1_valid
                 << ", \"expected\": " << q1_expected
                 << ", \"missed_deadline\": " << q1_missed
                 << ", \"unknown\": " << q1_unknown << "},\n"
                 << "  \"alice_q2\": {\"valid\": " << q2_valid
                 << ", \"expected\": " << q2_expected
                 << ", \"missed_deadline\": " << q2_missed
                 << ", \"unknown\": " << q2_unknown
                 << ", \"phase_correct\": " << q2_phase_ok << "}\n"
                 << "}\n";

    {
      auto const parent =
          std::filesystem::path{args.artifact_dir}.parent_path();
      auto const sibling_name =
          args.transport == "tcp" ? "udp/summary.json" : "tcp/summary.json";
      auto const sibling = parent / sibling_name;
      if (std::filesystem::exists(sibling)) {
        std::ofstream cmp(parent / "comparison.md");
        cmp << "# TCP vs UDP — ping_retry_count=0 runtime acceptance\n\n";
        cmp << "| metric | tcp | udp |\n";
        cmp << "| --- | --- | --- |\n";
        cmp << "| artifact | tcp/ | udp/ |\n";
        cmp << "| run order | first | second |\n";
        cmp.flush();
      }
    }

    std::cout << (prod_ok && harness_invalid_n == 0 && q2_query_failed == 0
                      ? "PASS"
                      : "FAIL")
              << " retry-count-zero-runtime cases=" << cases.size()
              << " valid=" << valid_n
              << " pre_deadline_retry_cases=" << pre_deadline_retry_cases
              << " post_deadline_recovery=" << post_deadline_recovery_cases
              << " production_fail=" << production_fail_n
              << " harness_invalid=" << harness_invalid_n
              << " alice_q2_query_failed=" << q2_query_failed << std::endl;

    StopChild(alice);
    StopChild(bob);
    return (prod_ok && harness_invalid_n == 0 && q2_query_failed == 0) ? 0
                                                                       : 7;

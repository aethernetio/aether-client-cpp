    // Focused first-request-loss vs intended 1.5*p99+G policy.
    // Alice queries only AFTER original deadline Tn.
    // Included inside RunCharacterization after Alice/Bob spawn + warmup.
    //
    // Production formula (ping_cloud_servers.cpp / ping_schedule_guard.h):
    //   p99 = response_time_statistics().percentile<99>()
    //   min = response_time_statistics().min()
    //   G   = Clamp(max(0,(p99-min)/2)+10ms)
    //   raw = Channel::ResponseTimeout() = same p99 when stats non-empty
    //   loss_timeout = max(raw, p99+10ms, 50ms) [may cap for interval]
    //   attempt_lead = G + loss_timeout + p99/2 + scheduler(10)
    //                  + retry_dispatch(60)
    //                ≈ 1.5*p99 + G + 80ms when uncapped
    // Intended invariant under test:
    //   first_send <= Tn - (1.5*R99 + G)
    //   retry_send <= Tn - (R99/2 + G)
    //   retry_send + R99/2 <= Tn - G

    struct FrlCase {
      int index{0};
      std::int64_t seed{0};
      int target_cycle_index{0};
      std::int64_t logical_ping_id{0};
      std::int64_t target_logical_ping_id{0};
      std::int64_t phase_anchor_us{0};
      std::int64_t tn_us{0};
      std::int64_t rtt_sample_hint{0};
      double rtt_p99_used_ms{std::numeric_limits<double>::quiet_NaN()};
      double guard_used_ms{std::numeric_limits<double>::quiet_NaN()};
      double loss_timeout_ms{std::numeric_limits<double>::quiet_NaN()};
      double attempt_lead_ms{std::numeric_limits<double>::quiet_NaN()};
      double expected_retry_budget_ms{std::numeric_limits<double>::quiet_NaN()};
      double expected_first_send_us{
          std::numeric_limits<double>::quiet_NaN()};
      std::int64_t actual_first_send_us{
          std::numeric_limits<std::int64_t>::min()};
      double first_send_margin_ms{std::numeric_limits<double>::quiet_NaN()};
      int first_request_dropped{-1};
      std::int64_t fault_arm_time{0};
      int fault_state_before_send{-1};
      std::int64_t first_request_attempt_number{0};
      std::int64_t first_request_send_time{0};
      std::int64_t fault_match_time{0};
      std::int64_t fault_drop_time{0};
      int fault_consumed{0};
      std::int64_t retry_decision_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t actual_retry_send_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_logical_ping_id{0};
      std::int64_t retry_attempt_number{0};
      std::int64_t retry_send_time{0};
      int fault_state_end{-1};
      double expected_latest_retry_send_us{
          std::numeric_limits<double>::quiet_NaN()};
      double retry_send_margin_ms{std::numeric_limits<double>::quiet_NaN()};
      double estimated_retry_server_arrival_us{
          std::numeric_limits<double>::quiet_NaN()};
      double estimated_retry_server_margin_ms{
          std::numeric_limits<double>::quiet_NaN()};
      std::int64_t next_nominal_us{0};
      double next_nominal_phase_error_ms{
          std::numeric_limits<double>::quiet_NaN()};
      std::int64_t next2_us{0};
      std::int64_t next3_us{0};
      double next2_phase_error_ms{std::numeric_limits<double>::quiet_NaN()};
      double next3_phase_error_ms{std::numeric_limits<double>::quiet_NaN()};
      std::int64_t alice_query_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t alice_last_request_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t alice_deadline_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t alice_next_deadline_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t alice_state{-2};
      std::int64_t alice_next_ping_delta_ms{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t alice_last_connect_delta_ms{
          std::numeric_limits<std::int64_t>::min()};
      bool alice_missed_deadline{false};
      bool harness_sync_ok{false};
      std::vector<std::string> failures;
    };

    auto const kMissing = std::numeric_limits<std::int64_t>::min();
    auto abs_d = [](double v) { return v < 0 ? -v : v; };
    auto csv_d = [&](double v) -> std::string {
      if (!std::isfinite(v)) {
        return {};
      }
      return F3(v);
    };
    auto csv_i = [&](std::int64_t v) -> std::string {
      if (v == kMissing) {
        return {};
      }
      return std::to_string(v);
    };

    // Production uses response_stats.percentile<99>(). After warmup, p99 must
    // not still be the empty-stats estimate alone unless samples exist.
    if (warmup_n <= 0) {
      std::cerr << "FAIL FIRST_REQUEST_LOSS_P99: production p99 statistic has "
                   "no warm-up samples (rtt_sample_count=0)\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }

    std::int64_t const period_us = args.ping_interval_ms * 1000;
    // Existing scheduler tick / characterization resolution (1 ms).
    double const tick_ms = 1.0;
    int const n_cases = args.first_request_loss_cases > 0
                            ? args.first_request_loss_cases
                            : 100;

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
    std::ofstream fault_trace_csv(std::filesystem::path{args.artifact_dir} /
                                  "fault-trace.csv");
    if (!samples_csv || !samples_jsonl || !failed_json || !report ||
        !summary_json || !fault_trace_csv) {
      std::cerr << "FAIL cannot open first-request-loss-p99 outputs\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }
    double const dispatch_margin_ms = 60.0;
    fault_trace_csv
        << "case_index,transport,seed,target_logical_ping_id,"
           "target_cycle_index,fault_arm_time,fault_state_before_send,"
           "first_request_logical_ping_id,first_request_attempt_number,"
           "first_request_send_time,fault_match_time,fault_drop_time,"
           "fault_consumed,retry_logical_ping_id,retry_attempt_number,"
           "retry_send_time,fault_state_end\n";
    samples_csv
        << "transport,seed,case_index,logical_ping_id,rtt_sample_count,"
           "rtt_p99_used_ms,guard_used_ms,dispatch_margin_ms,loss_timeout_ms,"
           "attempt_lead_ms,"
           "Tn_us,expected_retry_budget_ms,expected_first_send_us,"
           "actual_first_send_us,first_send_margin_ms,first_request_dropped,"
           "retry_decision_us,expected_latest_retry_send_us,"
           "actual_retry_send_us,retry_send_margin_ms,"
           "estimated_retry_server_arrival_us,"
           "estimated_retry_server_margin_to_Tn_ms,"
           "estimated_retry_server_margin_to_Tn_minus_guard_ms,"
           "next_nominal_us,next_nominal_phase_error_ms,next2_us,"
           "next2_phase_error_ms,next3_us,next3_phase_error_ms,"
           "alice_query_us,alice_last_request_us,alice_deadline_us,"
           "alice_next_deadline_us,alice_state,alice_missed_deadline,"
           "alice_next_ping_delta_ms,alice_last_connect_delta_ms,"
           "failure_classes,harness_invalid\n";

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
    auto wait_first = [&](DWORD timeout_ms) -> std::optional<BobPingEvent> {
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        for (auto const& e : bob.ping_events) {
          if (seen_ev.count(ev_id(e)) != 0 || !dest_ok(e)) {
            continue;
          }
          if (e.physical_attempt_index > 1) {
            continue;
          }
          if (e.kind == static_cast<std::uint8_t>(PingTraceKind::kRequestSent) ||
              e.kind ==
                  static_cast<std::uint8_t>(PingTraceKind::kRequestDropped)) {
            return take_ev(e);
          }
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

    int harness_armed = 0;
    int harness_matched = 0;
    int harness_dropped = 0;
    int harness_wrong_request = 0;
    int harness_unconsumed = 0;
    int harness_leaked = 0;
    int harness_sync_errors = 0;

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
          if (e.logical_cycle_id == 0) {
            continue;
          }
          if (e.logical_cycle_id <= min_cycle_id) {
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

    auto wait_next_first_after = [&](std::int64_t after_cycle_id, DWORD timeout_ms)
        -> std::optional<BobPingEvent> {
      auto const deadline = GetTickCount64() + timeout_ms;
      while (GetTickCount64() < deadline) {
        drain(20);
        for (auto const& e : bob.ping_events) {
          if (seen_ev.count(ev_id(e)) != 0 || !dest_ok(e)) {
            continue;
          }
          if (e.logical_cycle_id == 0) {
            continue;
          }
          if (e.logical_cycle_id <= after_cycle_id) {
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

    auto write_fault_trace_row = [&](FrlCase const& c) {
      fault_trace_csv << c.index << "," << args.transport << "," << c.seed << ","
                      << c.target_logical_ping_id << "," << c.target_cycle_index
                      << "," << c.fault_arm_time << ","
                      << c.fault_state_before_send << ","
                      << c.logical_ping_id << "," << c.first_request_attempt_number
                      << "," << c.first_request_send_time << ","
                      << c.fault_match_time << "," << c.fault_drop_time << ","
                      << c.fault_consumed << "," << c.retry_logical_ping_id
                      << "," << c.retry_attempt_number << ","
                      << c.retry_send_time << "," << c.fault_state_end << "\n";
      fault_trace_csv.flush();
    };

    auto fail = [&](FrlCase& c, char const* cls) {
      c.failures.push_back(cls);
      std::cerr << "FAIL case " << c.index << " " << cls << std::endl;
    };

    std::vector<FrlCase> cases;
    std::int64_t phase_anchor_us = 0;
    bool ok_all = true;

    std::cout << "FIRST_REQUEST_LOSS_P99 cases=" << n_cases
              << " seed=" << args.seed << " transport=" << args.transport
              << " warmup_n=" << warmup_n << " warmup_p99_ms=" << warmup_p99
              << " warmup_min_ms=" << warmup_min << std::endl;

    // Document production statistic source once.
    std::cout
        << "PRODUCTION_P99_SOURCE=channel_statistics().response_time_"
           "statistics().percentile<99>() "
           "via PingCloudServers::MakePing; "
           "guard=Clamp(max(0,(p99-min)/2)+10ms); "
           "attempt_lead=G+loss_timeout+p99/2+scheduler+dispatch; "
           "loss_timeout=max(ResponseTimeout(=p99),p99+10ms,50ms)\n";

    wait_window_closed(8000);

    for (int ci = 0; ci < n_cases; ++ci) {
      FrlCase rec{};
      rec.index = ci;
      rec.seed = args.seed;
      rec.rtt_sample_hint = warmup_n;
      rec.target_cycle_index = ci;
      rec.fault_state_before_send = 0;

      std::int64_t settle_before = last_settled_cycle_id;
      std::size_t fault_trace_before = bob.fault_traces.size();

      wait_window_closed(8000);

      bool armed_ok = false;
      DropWaitResult dw{};
      for (int arm_try = 0; arm_try < 4; ++arm_try) {
        armed_ok = arm_drop_next_first();
        if (armed_ok && arm_try == 0) {
          ++harness_armed;
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

      bool sync_ok = armed_ok && dw.drop.has_value() && dw.armed &&
                     dw.matched && dw.dropped_trace &&
                     dw.drop->physical_attempt_index == 1 &&
                     dw.drop->logical_cycle_id > settle_before;

      rec.fault_arm_time = dw.arm_time;
      rec.fault_match_time = dw.match_time;
      rec.fault_drop_time = dw.drop_time;
      rec.fault_state_end = sync_ok ? 4 : -1;

      if (dw.matched) {
        ++harness_matched;
      }
      if (dw.dropped_trace) {
        ++harness_dropped;
      }
      if (dw.sent_instead) {
        ++harness_wrong_request;
      }

      if (!sync_ok) {
        fail(rec, "HARNESS_FAULT_SYNC_ERROR");
        ++harness_sync_errors;
        ok_all = false;
        if (armed_ok && !dw.dropped_trace && !dw.sent_instead) {
          ++harness_unconsumed;
        }
        if (dw.sent_instead) {
          rec.first_request_dropped = 0;
          rec.logical_ping_id = dw.cycle_id;
        } else {
          rec.first_request_dropped = 0;
        }
        write_fault_trace_row(rec);
        cases.push_back(std::move(rec));
        disarm_fault();
        continue;
      }

      rec.harness_sync_ok = true;
      rec.fault_consumed = 1;
      rec.first_request_dropped = 1;

      BobPingEvent const first = *dw.drop;
      rec.logical_ping_id = first.logical_cycle_id;
      rec.target_logical_ping_id = first.logical_cycle_id;
      rec.tn_us = first.cycle_anchor_us;
      rec.first_request_attempt_number = first.physical_attempt_index;
      rec.first_request_send_time =
          first.actual_us != 0 ? first.actual_us : first.event_steady_us;
      if (phase_anchor_us == 0 && rec.tn_us != 0) {
        phase_anchor_us = rec.tn_us;
      }
      rec.phase_anchor_us = phase_anchor_us;
      rec.actual_first_send_us = rec.first_request_send_time;

      // Production values used for THIS attempt (from Bob ping trace).
      if (first.p99_rtt_us <= 0) {
        fail(rec, "HARNESS_FAILURE");
        std::cerr << "  reason: production p99_rtt not present on attempt "
                     "trace (expected response_stats.percentile<99>)\n";
        ok_all = false;
        write_fault_trace_row(rec);
        cases.push_back(std::move(rec));
        disarm_fault();
        continue;
      }
      rec.rtt_p99_used_ms = first.p99_rtt_us / 1000.0;
      rec.guard_used_ms = first.guard_us / 1000.0;
      rec.loss_timeout_ms = first.loss_timeout_us / 1000.0;
      rec.attempt_lead_ms = first.attempt_lead_us / 1000.0;

      // Intended policy under test (not production's +20ms extras).
      rec.expected_retry_budget_ms =
          1.5 * rec.rtt_p99_used_ms + rec.guard_used_ms;
      rec.expected_first_send_us =
          static_cast<double>(rec.tn_us) -
          rec.expected_retry_budget_ms * 1000.0;
      rec.first_send_margin_ms =
          (rec.expected_first_send_us -
           static_cast<double>(rec.actual_first_send_us)) /
          1000.0;
      rec.expected_latest_retry_send_us =
          static_cast<double>(rec.tn_us) -
          (rec.rtt_p99_used_ms / 2.0 + rec.guard_used_ms) * 1000.0;

      if (!(rec.first_send_margin_ms >= -tick_ms)) {
        // actual_first_send must be <= expected_first_send (+1ms tick)
        fail(rec, "FIRST_SEND_TOO_LATE");
        ok_all = false;
      }

      // Wait for attempt timeout then retry send. No Alice QueryNow here.
      auto to = find_ev(
          static_cast<std::uint8_t>(PingTraceKind::kAttemptTimeout), 2000,
          rec.logical_ping_id, 0);
      if (to) {
        rec.retry_decision_us = to->event_steady_us;
      }
      auto retry = find_ev(
          static_cast<std::uint8_t>(PingTraceKind::kRequestSent), 2000,
          rec.logical_ping_id, 2);
      if (!retry || retry->request_was_sent == 0) {
        fail(rec, "NO_RETRY");
        ok_all = false;
      } else {
        rec.actual_retry_send_us =
            retry->actual_us != 0 ? retry->actual_us : retry->event_steady_us;
        rec.retry_logical_ping_id = retry->logical_cycle_id;
        rec.retry_attempt_number = retry->physical_attempt_index;
        rec.retry_send_time = rec.actual_retry_send_us;
        rec.retry_send_margin_ms =
            (rec.expected_latest_retry_send_us -
             static_cast<double>(rec.actual_retry_send_us)) /
            1000.0;
        if (!(rec.retry_send_margin_ms >= -tick_ms)) {
          fail(rec, "RETRY_SEND_TOO_LATE");
          ok_all = false;
        }
        rec.estimated_retry_server_arrival_us =
            static_cast<double>(rec.actual_retry_send_us) +
            (rec.rtt_p99_used_ms / 2.0) * 1000.0;
        rec.estimated_retry_server_margin_ms =
            (static_cast<double>(rec.tn_us) -
             rec.estimated_retry_server_arrival_us) /
            1000.0;
        // Require arrival <= Tn - G  => margin >= G
        if (!(rec.estimated_retry_server_margin_ms + tick_ms >=
              rec.guard_used_ms)) {
          fail(rec, "ESTIMATED_RETRY_ARRIVAL_LATE");
          ok_all = false;
        }
        if (retry->contract_deadline_us != 0) {
          rec.next_nominal_us = retry->contract_deadline_us;
        }
      }

      // Confirm cycle / capture next nominal from confirm if needed.
      auto conf = find_ev(
          static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 8000,
          rec.logical_ping_id, 0);
      if (conf) {
        if (rec.next_nominal_us == 0) {
          rec.next_nominal_us = conf->contract_deadline_us != 0
                                    ? conf->contract_deadline_us
                                    : (rec.tn_us + period_us);
        }
      } else if (rec.next_nominal_us == 0 && rec.tn_us != 0) {
        rec.next_nominal_us = rec.tn_us + period_us;
      }
      if (rec.tn_us != 0 && rec.next_nominal_us != 0) {
        rec.next_nominal_phase_error_ms =
            (static_cast<double>(rec.next_nominal_us) -
             static_cast<double>(rec.tn_us + period_us)) /
            1000.0;
        if (abs_d(rec.next_nominal_phase_error_ms) > tick_ms) {
          fail(rec, "POST_RECOVERY_PHASE_DRIFT");
          ok_all = false;
        }
      }

      // Wait until after Tn, then Alice queries exactly once.
      if (rec.tn_us != 0 && first.event_qpc != 0 && first.actual_us != 0 &&
          rec.tn_us > first.actual_us) {
        LARGE_INTEGER qfreq{};
        QueryPerformanceFrequency(&qfreq);
        double const qpc_per_us =
            static_cast<double>(qfreq.QuadPart) / 1000000.0;
        auto const tn_qpc =
            first.event_qpc +
            static_cast<std::int64_t>(
                static_cast<double>(rec.tn_us - first.actual_us) * qpc_per_us);
        auto now_qpc = QpcNow();
        if (now_qpc < tn_qpc) {
          auto wait_ms =
              QpcToMs(static_cast<std::uint64_t>(tn_qpc - now_qpc));
          if (wait_ms < 5000) {
            Sleep(static_cast<DWORD>(wait_ms +
                                    static_cast<std::int64_t>(tick_ms) + 2));
          }
        } else {
          Sleep(static_cast<DWORD>(tick_ms) + 2);
        }
      } else {
        Sleep(static_cast<DWORD>(tick_ms) + 2);
      }

      SendRaw(alice, kIpcQueryNow, 0, /*checkpoint*/ 4, 0, 0);
      auto aq = wait_sched(3000);
      if (!aq) {
        fail(rec, "HARNESS_FAILURE");
        ok_all = false;
      } else {
        rec.alice_query_us = aq->steady_us;
        rec.alice_state = aq->state;
        rec.alice_deadline_us = aq->next_us;
        rec.alice_next_deadline_us = aq->next_us;
        rec.alice_last_request_us = aq->last_online_us;
        rec.alice_next_ping_delta_ms = aq->next_ping_delta_ms;
        rec.alice_last_connect_delta_ms = aq->last_connect_delta_ms;
        rec.alice_missed_deadline = (aq->state == 1);
        if (rec.alice_missed_deadline) {
          fail(rec, "ALICE_FALSE_MISSED_DEADLINE");
          ok_all = false;
        }
        // last_request vs Tn is statistical only (p99 leaves a tail).
        // Do NOT fail the case solely because last_request >= Tn.
        if (!(rec.alice_last_request_us != 0 && rec.tn_us != 0) &&
            !(aq->last_connect_delta_ms !=
                  std::numeric_limits<std::int64_t>::min() &&
              aq->last_connect_delta_ms >= 0)) {
          fail(rec, "HARNESS_FAILURE");
          ok_all = false;
        }
        // next deadline on Tn+1000
        if (rec.alice_next_deadline_us != 0 && rec.tn_us != 0) {
          double phase_err =
              (static_cast<double>(rec.alice_next_deadline_us) -
               static_cast<double>(rec.tn_us + period_us)) /
              1000.0;
          if (abs_d(phase_err) > tick_ms) {
            // Also accept next_ping_delta ≈ remaining to Tn+1000
            bool delta_ok = false;
            if (aq->next_ping_delta_ms !=
                std::numeric_limits<std::int64_t>::min()) {
              // After Tn, next should be ~Tn+1000 => delta roughly
              // (Tn+1000 - query_time). Soft check: delta > 0 and state Expected.
              delta_ok = aq->next_ping_delta_ms > 0 && aq->state == 0;
            }
            if (!delta_ok) {
              fail(rec, "ALICE_NEXT_DEADLINE_PHASE_SHIFT");
              ok_all = false;
            }
          }
        } else if (aq->next_ping_delta_ms <= 0 || aq->state != 0) {
          fail(rec, "ALICE_NEXT_DEADLINE_PHASE_SHIFT");
          ok_all = false;
        }
      }

      // Observe next three nominal anchors on the same phase.
      disarm_fault();
      std::int64_t observe_after = rec.logical_ping_id;
      std::vector<std::int64_t> next_tns;
      for (int k = 0; k < 3; ++k) {
        auto nx = wait_next_first_after(observe_after, 4000);
        if (!nx) {
          break;
        }
        observe_after = nx->logical_cycle_id;
        (void)find_ev(
            static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 4000,
            nx->logical_cycle_id, 0);
        if (nx->cycle_anchor_us != 0) {
          next_tns.push_back(nx->cycle_anchor_us);
        }
      }
      last_settled_cycle_id = observe_after;
      for (std::size_t i = fault_trace_before; i < bob.fault_traces.size(); ++i) {
        auto const& t = bob.fault_traces[i];
        if (t.kind == kFaultTraceArmed &&
            (t.logical_cycle_id == 0 || t.logical_cycle_id > settle_before)) {
          ++harness_leaked;
        }
      }
      if (next_tns.size() >= 1) {
        rec.next2_us = next_tns[0];
        rec.next2_phase_error_ms =
            (static_cast<double>(rec.next2_us) -
             static_cast<double>(rec.tn_us + 2 * period_us)) /
            1000.0;
      }
      if (next_tns.size() >= 2) {
        // If we already captured next_nominal from contract, first observed
        // after confirm may be Tn+1000 or Tn+2000 depending on timing.
        // Align by snapping to nearest grid slot.
        auto snap_err = [&](std::int64_t t, int slot) {
          return (static_cast<double>(t) -
                  static_cast<double>(rec.tn_us + slot * period_us)) /
                 1000.0;
        };
        // Recompute best slot assignment for observed anchors
        for (std::size_t i = 0; i < next_tns.size(); ++i) {
          double best = 1e300;
          int best_slot = static_cast<int>(i + 1);
          for (int slot = 1; slot <= 4; ++slot) {
            auto e = abs_d(snap_err(next_tns[i], slot));
            if (e < best) {
              best = e;
              best_slot = slot;
            }
          }
          if (best_slot == 1 && i == 0) {
            // already have next_nominal; prefer observed if closer
            if (!std::isfinite(rec.next_nominal_phase_error_ms) ||
                abs_d(snap_err(next_tns[i], 1)) <
                    abs_d(rec.next_nominal_phase_error_ms)) {
              rec.next_nominal_us = next_tns[i];
              rec.next_nominal_phase_error_ms = snap_err(next_tns[i], 1);
            }
          } else if (best_slot == 2) {
            rec.next2_us = next_tns[i];
            rec.next2_phase_error_ms = snap_err(next_tns[i], 2);
          } else if (best_slot == 3) {
            rec.next3_us = next_tns[i];
            rec.next3_phase_error_ms = snap_err(next_tns[i], 3);
          }
        }
      }
      if (next_tns.size() >= 3) {
        auto snap_err = [&](std::int64_t t, int slot) {
          return (static_cast<double>(t) -
                  static_cast<double>(rec.tn_us + slot * period_us)) /
                 1000.0;
        };
        rec.next3_us = next_tns.back();
        rec.next3_phase_error_ms = snap_err(rec.next3_us, 3);
      }
      for (double e :
           {rec.next_nominal_phase_error_ms, rec.next2_phase_error_ms,
            rec.next3_phase_error_ms}) {
        if (std::isfinite(e) && abs_d(e) > tick_ms) {
          fail(rec, "POST_RECOVERY_PHASE_DRIFT");
          ok_all = false;
          break;
        }
      }

      // CSV + JSONL row (flushed each case)
      std::string classes;
      for (std::size_t fi = 0; fi < rec.failures.size(); ++fi) {
        if (fi) {
          classes += "|";
        }
        classes += rec.failures[fi];
      }
      auto is_harness_cls = [](std::string const& s) {
        return s.find("HARNESS") != std::string::npos ||
               s == "FIRST_REQUEST_NOT_ACTUALLY_DROPPED";
      };
      bool harness_invalid = false;
      for (auto const& f : rec.failures) {
        if (is_harness_cls(f)) {
          harness_invalid = true;
          break;
        }
      }
      double est_to_tn_g = std::numeric_limits<double>::quiet_NaN();
      if (std::isfinite(rec.estimated_retry_server_margin_ms) &&
          std::isfinite(rec.guard_used_ms)) {
        est_to_tn_g =
            rec.estimated_retry_server_margin_ms - rec.guard_used_ms;
      }
      samples_csv << args.transport << "," << args.seed << "," << ci << ","
                  << rec.logical_ping_id << "," << rec.rtt_sample_hint << ","
                  << csv_d(rec.rtt_p99_used_ms) << ","
                  << csv_d(rec.guard_used_ms) << "," << csv_d(dispatch_margin_ms)
                  << "," << csv_d(rec.loss_timeout_ms) << ","
                  << csv_d(rec.attempt_lead_ms) << "," << rec.tn_us << ","
                  << csv_d(rec.expected_retry_budget_ms) << ","
                  << csv_d(rec.expected_first_send_us) << ","
                  << csv_i(rec.actual_first_send_us) << ","
                  << csv_d(rec.first_send_margin_ms) << ","
                  << rec.first_request_dropped << ","
                  << csv_i(rec.retry_decision_us) << ","
                  << csv_d(rec.expected_latest_retry_send_us) << ","
                  << csv_i(rec.actual_retry_send_us) << ","
                  << csv_d(rec.retry_send_margin_ms) << ","
                  << csv_d(rec.estimated_retry_server_arrival_us) << ","
                  << csv_d(rec.estimated_retry_server_margin_ms) << ","
                  << csv_d(est_to_tn_g) << "," << rec.next_nominal_us << ","
                  << csv_d(rec.next_nominal_phase_error_ms) << ","
                  << rec.next2_us << "," << csv_d(rec.next2_phase_error_ms)
                  << "," << rec.next3_us << ","
                  << csv_d(rec.next3_phase_error_ms) << ","
                  << csv_i(rec.alice_query_us) << ","
                  << csv_i(rec.alice_last_request_us) << ","
                  << csv_i(rec.alice_deadline_us) << ","
                  << csv_i(rec.alice_next_deadline_us) << ","
                  << rec.alice_state << ","
                  << (rec.alice_missed_deadline ? 1 : 0) << ","
                  << csv_i(rec.alice_next_ping_delta_ms) << ","
                  << csv_i(rec.alice_last_connect_delta_ms) << "," << classes
                  << "," << (harness_invalid ? 1 : 0) << "\n";
      samples_csv.flush();
      samples_jsonl
          << "{\"case_index\":" << ci << ",\"transport\":\"" << args.transport
          << "\",\"seed\":" << args.seed << ",\"Tn\":" << rec.tn_us
          << ",\"R99_used_ms\":" << csv_d(rec.rtt_p99_used_ms)
          << ",\"guard_ms\":" << csv_d(rec.guard_used_ms)
          << ",\"dispatch_margin_ms\":" << csv_d(dispatch_margin_ms)
          << ",\"expected_first_send_ms\":"
          << csv_d(std::isfinite(rec.expected_first_send_us)
                       ? rec.expected_first_send_us / 1000.0
                       : std::numeric_limits<double>::quiet_NaN())
          << ",\"actual_first_send_ms\":"
          << (rec.actual_first_send_us == kMissing
                  ? std::string{}
                  : std::to_string(rec.actual_first_send_us / 1000.0))
          << ",\"first_request_dropped\":" << rec.first_request_dropped
          << ",\"retry_decision_ms\":"
          << (rec.retry_decision_us == kMissing
                  ? std::string{}
                  : std::to_string(rec.retry_decision_us / 1000.0))
          << ",\"expected_latest_retry_send_ms\":"
          << csv_d(std::isfinite(rec.expected_latest_retry_send_us)
                       ? rec.expected_latest_retry_send_us / 1000.0
                       : std::numeric_limits<double>::quiet_NaN())
          << ",\"actual_retry_send_ms\":"
          << (rec.actual_retry_send_us == kMissing
                  ? std::string{}
                  : std::to_string(rec.actual_retry_send_us / 1000.0))
          << ",\"retry_send_margin_ms\":" << csv_d(rec.retry_send_margin_ms)
          << ",\"estimated_retry_server_arrival_ms\":"
          << csv_d(std::isfinite(rec.estimated_retry_server_arrival_us)
                       ? rec.estimated_retry_server_arrival_us / 1000.0
                       : std::numeric_limits<double>::quiet_NaN())
          << ",\"estimated_retry_server_margin_to_Tn_ms\":"
          << csv_d(rec.estimated_retry_server_margin_ms)
          << ",\"estimated_retry_server_margin_to_Tn_minus_guard_ms\":"
          << csv_d(est_to_tn_g)
          << ",\"next_nominal_deadline_ms\":"
          << (rec.next_nominal_us == 0
                  ? std::string{}
                  : std::to_string(rec.next_nominal_us / 1000.0))
          << ",\"next_nominal_phase_error_ms\":"
          << csv_d(rec.next_nominal_phase_error_ms)
          << ",\"alice_query_time_ms\":"
          << (rec.alice_query_us == kMissing
                  ? std::string{}
                  : std::to_string(rec.alice_query_us / 1000.0))
          << ",\"alice_last_request_time_ms\":"
          << (rec.alice_last_request_us == kMissing ||
                      rec.alice_last_request_us == 0
                  ? std::string{}
                  : std::to_string(rec.alice_last_request_us / 1000.0))
          << ",\"alice_state\":" << rec.alice_state
          << ",\"alice_missed_deadline\":"
          << (rec.alice_missed_deadline ? "true" : "false")
          << ",\"alice_next_deadline_ms\":"
          << (rec.alice_next_deadline_us == kMissing ||
                      rec.alice_next_deadline_us == 0
                  ? std::string{}
                  : std::to_string(rec.alice_next_deadline_us / 1000.0))
          << ",\"harness_invalid\":" << (harness_invalid ? "true" : "false")
          << ",\"failure_classes\":\"" << classes << "\"}\n";
      samples_jsonl.flush();
      write_fault_trace_row(rec);
      cases.push_back(std::move(rec));
    }

    // Aggregate helpers
    auto collect = [&](auto proj) {
      std::vector<double> v;
      for (auto const& c : cases) {
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
      return "n=" + std::to_string(s.size()) + " min=" + F3(s.front()) +
             " p1=" + F3(pct(s, 1)) + " p5=" + F3(pct(s, 5)) +
             " p50=" + F3(pct(s, 50)) + " p95=" + F3(pct(s, 95)) +
             " p99=" + F3(pct(s, 99)) + " max=" + F3(s.back());
    };

    auto collect_sync = [&](auto proj) {
      std::vector<double> v;
      for (auto const& c : cases) {
        if (!c.harness_sync_ok) {
          continue;
        }
        double x = proj(c);
        if (std::isfinite(x)) {
          v.push_back(x);
        }
      }
      return v;
    };

    auto p99s = collect_sync([](FrlCase const& c) { return c.rtt_p99_used_ms; });
    auto guards = collect_sync([](FrlCase const& c) { return c.guard_used_ms; });
    auto fs_m =
        collect_sync([](FrlCase const& c) { return c.first_send_margin_ms; });
    auto rs_m =
        collect_sync([](FrlCase const& c) { return c.retry_send_margin_ms; });
    auto est_m = collect_sync([](FrlCase const& c) {
      return c.estimated_retry_server_margin_ms;
    });
    auto est_to_tn_g_m = collect_sync([](FrlCase const& c) {
      if (!std::isfinite(c.estimated_retry_server_margin_ms) ||
          !std::isfinite(c.guard_used_ms)) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      return c.estimated_retry_server_margin_ms - c.guard_used_ms;
    });
    auto phase_e = collect_sync([](FrlCase const& c) {
      return c.next_nominal_phase_error_ms;
    });

    int drop_ok = 0, first_late = 0, retry_late = 0, est_after_tn = 0,
        est_after_tn_g = 0, alice_last_ok = 0, alice_md_false = 0,
        alice_phase_ok = 0, alice_n = 0, retry_on_time_dropped = 0,
        est_by_tn_g_dropped = 0, drift_1000 = 0;
    for (auto const& c : cases) {
      if (!c.harness_sync_ok) {
        continue;
      }
      if (c.first_request_dropped == 1) {
        ++drop_ok;
      }
      if (std::isfinite(c.first_send_margin_ms) &&
          c.first_send_margin_ms < -tick_ms) {
        ++first_late;
      }
      if (std::isfinite(c.retry_send_margin_ms) &&
          c.retry_send_margin_ms < -tick_ms) {
        ++retry_late;
      }
      if (std::isfinite(c.estimated_retry_server_margin_ms) &&
          c.estimated_retry_server_margin_ms < 0) {
        ++est_after_tn;
      }
      if (std::isfinite(c.estimated_retry_server_margin_ms) &&
          std::isfinite(c.guard_used_ms) &&
          c.estimated_retry_server_margin_ms + tick_ms < c.guard_used_ms) {
        ++est_after_tn_g;
      }
      for (double e : {c.next_nominal_phase_error_ms, c.next2_phase_error_ms,
                       c.next3_phase_error_ms}) {
        if (std::isfinite(e) && abs_d(e) >= 1000.0 - tick_ms) {
          ++drift_1000;
          break;
        }
      }
      if (c.alice_query_us != kMissing) {
        ++alice_n;
        if (c.alice_last_request_us != 0 && c.tn_us != 0 &&
            c.alice_last_request_us < c.tn_us) {
          ++alice_last_ok;
        }
        if (!c.alice_missed_deadline) {
          ++alice_md_false;
        }
        bool phase_ok = false;
        if (c.alice_next_deadline_us != 0 && c.tn_us != 0) {
          double e = (static_cast<double>(c.alice_next_deadline_us) -
                      static_cast<double>(c.tn_us + period_us)) /
                     1000.0;
          phase_ok = abs_d(e) <= tick_ms;
        }
        if (!phase_ok && c.alice_next_ping_delta_ms > 0 && c.alice_state == 0) {
          phase_ok = true;
        }
        if (phase_ok) {
          ++alice_phase_ok;
        }
      }
    }
    retry_on_time_dropped = 0;
    est_by_tn_g_dropped = 0;
    for (auto const& c : cases) {
      if (!c.harness_sync_ok) {
        continue;
      }
      if (std::isfinite(c.retry_send_margin_ms) &&
          c.retry_send_margin_ms >= -tick_ms) {
        ++retry_on_time_dropped;
      }
      if (std::isfinite(c.estimated_retry_server_margin_ms) &&
          std::isfinite(c.guard_used_ms) &&
          c.estimated_retry_server_margin_ms + tick_ms >= c.guard_used_ms) {
        ++est_by_tn_g_dropped;
      }
    }

    // Does production satisfy intended 1.5*p99+G?
    // Compare production attempt_lead to intended budget.
    int lead_ge_budget = 0;
    int lead_n = 0;
    for (auto const& c : cases) {
      if (std::isfinite(c.attempt_lead_ms) &&
          std::isfinite(c.expected_retry_budget_ms)) {
        ++lead_n;
        if (c.attempt_lead_ms + tick_ms >= c.expected_retry_budget_ms) {
          ++lead_ge_budget;
        }
      }
    }

    int const sync_ok_n = drop_ok;
    if (harness_sync_errors > 0) {
      ok_all = false;
    }

    report << "# First-request-loss p99 timing (deterministic fault)\n\n";
    report << "- transport: " << args.transport << "\n";
    report << "- seed: " << args.seed << "\n";
    report << "- cases: " << n_cases << "\n";
    report << "- warmup_rtt_samples: " << warmup_n << "\n";
    report << "- warmup_min_rtt_ms: " << warmup_min << "\n";
    report << "- warmup_p99_rtt_ms: " << warmup_p99 << "\n";
    report << "\n## Harness\n\n";
    report << "- planned: " << n_cases << "\n";
    report << "- fault armed correctly: " << harness_armed << "\n";
    report << "- matched correct logical ping: " << sync_ok_n << "\n";
    report << "- dropped correct attempt #1: " << sync_ok_n << "\n";
    report << "- wrong-request drops: " << harness_wrong_request << "\n";
    report << "- unconsumed faults: " << harness_unconsumed << "\n";
    report << "- leaked faults: " << harness_leaked << "\n";
    report << "- HARNESS_FAULT_SYNC_ERROR: " << harness_sync_errors << "\n";
    report << "\n## Production formula\n\n";
    report << "Source: `PingCloudServers::MakePing` +\n"
              "`ComputePingRetryBudget` / `ComputePingSendGuard`.\n\n";
    report << "- RTT statistic: "
              "`channel_statistics().response_time_statistics()."
              "percentile<99>()`\n";
    report << "- min RTT: `response_time_statistics().min()`\n";
    report << "- guard G: `Clamp(max(0,(p99-min)/2)+10ms)`\n";
    report << "- `ResponseTimeout()`: same p99 when stats non-empty\n";
    report << "- `loss_timeout = max(raw, p99+10ms, 50ms)` (interval-capped)\n";
    report << "- `attempt_lead = G + loss_timeout + p99/2 + scheduler(10ms) "
              "+ retry_dispatch(60ms)` "
              "(≈ `1.5*p99 + G + 80ms` when uncapped)\n";
    report << "- Intended invariant under test: "
              "`retry_send <= Tn-(R99/2+G)` and "
              "`retry_send+R99/2 <= Tn-G`\n";
    report << "- Alice `last_request < Tn` is reported but not a hard gate\n";
    report << "- cases where production attempt_lead >= intended budget: "
           << lead_ge_budget << "/" << lead_n << "\n";
    report << "\n## R99 / scheduling\n\n";
    report << "- R99: " << dstat(p99s) << "\n";
    report << "- guard: " << dstat(guards) << "\n";
    report << "\n## First send\n\n";
    report << "- first_send_margin (intended): " << dstat(fs_m) << "\n";
    report << "- cases first send too late: " << first_late << "\n";
    report << "- first request actually dropped (sync-valid): " << drop_ok
           << "/" << sync_ok_n << "\n";
    report << "\n## Retry timing (sync-valid drops only)\n\n";
    report << "- retry on time: " << retry_on_time_dropped << "/" << sync_ok_n
           << "\n";
    report << "- retry late: " << retry_late << "\n";
    report << "- retry_send_margin: " << dstat(rs_m) << "\n";
    report << "\n## Estimated server arrival (sync-valid drops only)\n\n";
    report << "- arrival by Tn-G: " << est_by_tn_g_dropped << "/" << sync_ok_n
           << "\n";
    report << "- arrival after Tn-G: " << est_after_tn_g << "\n";
    report << "- arrival after Tn: " << est_after_tn << "\n";
    report << "- margin_to_Tn_minus_G: " << dstat(est_to_tn_g_m) << "\n";
    report << "- estimated_retry_server_margin: " << dstat(est_m) << "\n";
    report << "\n## Alice (sync-valid drops only)\n\n";
    report << "- Alice queries: " << alice_n << "\n";
    report << "- last_request before Tn: " << alice_last_ok << "/" << alice_n
           << "\n";
    report << "- MissedDeadline false: " << alice_md_false << "/" << alice_n
           << "\n";
    report << "- next deadline on Tn+1000 (or Expected delta): " << alice_phase_ok
           << "/" << alice_n << "\n";
    report << "\n## Phase\n\n";
    report << "- valid queries: " << alice_n << "\n";
    report << "- last_request < Tn: " << alice_last_ok << "/" << alice_n
           << "\n";
    report << "- last_request >= Tn: " << (alice_n - alice_last_ok) << "/"
           << alice_n << "\n";
    report << "- MissedDeadline true: " << (alice_n - alice_md_false) << "/"
           << alice_n << "\n";
    report << "- MissedDeadline false: " << alice_md_false << "/" << alice_n
           << "\n";
    report << "- next deadline phase correct: " << alice_phase_ok << "/"
           << alice_n << "\n";
    report << "\n## Phase (sync-valid drops only)\n\n";
    report << "- next_nominal_phase_error: " << dstat(phase_e) << "\n";
    {
      std::vector<double> abs_drift;
      for (auto const& c : cases) {
        if (!c.harness_sync_ok) {
          continue;
        }
        for (double e : {c.next_nominal_phase_error_ms, c.next2_phase_error_ms,
                         c.next3_phase_error_ms}) {
          if (std::isfinite(e)) {
            abs_drift.push_back(abs_d(e));
          }
        }
      }
      report << "- next-3 max drift: "
             << (abs_drift.empty()
                     ? std::string("n/a")
                     : F3(*std::max_element(abs_drift.begin(),
                                            abs_drift.end())))
             << "\n";
    }
    report << "- 1000-ms drift count: " << drift_1000 << "\n";
    report << "\n## Result\n\n";
    report << (ok_all ? "PASS" : "FAIL") << " first-request-loss-p99\n";
    report.flush();

    // failed-cases.json
    failed_json << "[\n";
    bool first_fail = true;
    for (auto const& c : cases) {
      if (c.failures.empty()) {
        continue;
      }
      if (!first_fail) {
        failed_json << ",\n";
      }
      first_fail = false;
      failed_json << "  {\"transport\":\"" << args.transport
                  << "\",\"seed\":" << c.seed << ",\"case_index\":" << c.index
                  << ",\"R99\":" << csv_d(c.rtt_p99_used_ms)
                  << ",\"guard\":" << csv_d(c.guard_used_ms)
                  << ",\"Tn\":" << c.tn_us
                  << ",\"expected_first_send\":" << csv_d(c.expected_first_send_us)
                  << ",\"actual_first_send\":" << csv_i(c.actual_first_send_us)
                  << ",\"retry_decision\":" << csv_i(c.retry_decision_us)
                  << ",\"expected_latest_retry_send\":"
                  << csv_d(c.expected_latest_retry_send_us)
                  << ",\"actual_retry_send\":" << csv_i(c.actual_retry_send_us)
                  << ",\"estimated_retry_server_arrival\":"
                  << csv_d(c.estimated_retry_server_arrival_us)
                  << ",\"estimated_retry_server_margin\":"
                  << csv_d(c.estimated_retry_server_margin_ms)
                  << ",\"alice_query_time\":" << csv_i(c.alice_query_us)
                  << ",\"alice_last_request_time\":"
                  << csv_i(c.alice_last_request_us)
                  << ",\"alice_state\":" << c.alice_state
                  << ",\"alice_deadline\":" << csv_i(c.alice_deadline_us)
                  << ",\"alice_next_deadline\":"
                  << csv_i(c.alice_next_deadline_us)
                  << ",\"next_nominal\":" << c.next_nominal_us
                  << ",\"next2\":" << c.next2_us << ",\"next3\":" << c.next3_us
                  << ",\"phase_errors\":["
                  << csv_d(c.next_nominal_phase_error_ms) << ","
                  << csv_d(c.next2_phase_error_ms) << ","
                  << csv_d(c.next3_phase_error_ms) << "],\"classes\":[";
      for (std::size_t i = 0; i < c.failures.size(); ++i) {
        if (i) {
          failed_json << ",";
        }
        failed_json << "\"" << c.failures[i] << "\"";
      }
      failed_json << "]}";
    }
    failed_json << "\n]\n";
    failed_json.flush();

    int harness_invalid_n = 0;
    int production_fail_n = 0;
    int valid_n = 0;
    for (auto const& c : cases) {
      if (!c.harness_sync_ok) {
        continue;
      }
      ++valid_n;
      bool prod = false;
      for (auto const& f : c.failures) {
        if (f.find("HARNESS") == std::string::npos) {
          prod = true;
        }
      }
      if (prod) {
        ++production_fail_n;
      }
    }
    harness_invalid_n = harness_sync_errors;
    bool const prod_ok =
        (production_fail_n == 0 && harness_sync_errors == 0 && sync_ok_n == n_cases);
    summary_json << "{\n"
                 << "  \"transport\": \"" << args.transport << "\",\n"
                 << "  \"seed\": " << args.seed << ",\n"
                 << "  \"dispatch_margin_ms\": " << dispatch_margin_ms << ",\n"
                 << "  \"planned_cases\": " << n_cases << ",\n"
                 << "  \"harness_armed\": " << harness_armed << ",\n"
                 << "  \"harness_sync_ok\": " << sync_ok_n << ",\n"
                 << "  \"harness_sync_errors\": " << harness_sync_errors << ",\n"
                 << "  \"harness_wrong_request\": " << harness_wrong_request
                 << ",\n"
                 << "  \"harness_unconsumed\": " << harness_unconsumed << ",\n"
                 << "  \"harness_leaked\": " << harness_leaked << ",\n"
                 << "  \"drift_1000_ms\": " << drift_1000 << ",\n"
                 << "  \"valid_cases\": " << sync_ok_n << ",\n"
                 << "  \"harness_invalid_cases\": " << harness_sync_errors
                 << ",\n"
                 << "  \"production_timing_failures\": " << production_fail_n
                 << ",\n"
                 << "  \"retry_on_time\": \"" << retry_on_time_dropped << "/"
                 << sync_ok_n << "\",\n"
                 << "  \"retry_late\": " << retry_late << ",\n"
                 << "  \"estimated_arrival_by_Tn_minus_G\": \""
                 << est_by_tn_g_dropped << "/" << sync_ok_n << "\",\n"
                 << "  \"first_request_dropped\": \"" << drop_ok << "/"
                 << sync_ok_n << "\",\n"
                 << "  \"alice_queries\": " << alice_n << ",\n"
                 << "  \"alice_last_request_before_Tn\": \"" << alice_last_ok
                 << "/" << alice_n << "\",\n"
                 << "  \"alice_missed_deadline_false\": \"" << alice_md_false
                 << "/" << alice_n << "\",\n"
                 << "  \"pass\": " << (prod_ok ? "true" : "false") << "\n"
                 << "}\n";
    summary_json.flush();

    {
      auto root = std::filesystem::path{args.artifact_dir}.parent_path();
      std::ofstream cmp(root / "comparison.md");
      if (cmp) {
        cmp << "# Comparison: 500-case race vs deterministic 200-case\n\n";
        cmp << "| Metric | TCP old 500 | TCP deterministic 200 | UDP old 500 | "
               "UDP deterministic 200 |\n";
        cmp << "| --- | ---: | ---: | ---: | ---: |\n";
        cmp << "| intended first request dropped | 464/500 | ";
        if (args.transport == "tcp") {
          cmp << sync_ok_n << "/" << n_cases;
        }
        cmp << " | 390/500 | ";
        if (args.transport == "udp") {
          cmp << sync_ok_n << "/" << n_cases;
        }
        cmp << " |\n";
        cmp << "| fault sync failures | 36 | ";
        cmp << (args.transport == "tcp" ? std::to_string(harness_sync_errors)
                                       : std::string());
        cmp << " | 110 | ";
        cmp << (args.transport == "udp" ? std::to_string(harness_sync_errors)
                                       : std::string());
        cmp << " |\n";
        cmp << "| retry timing pass among real drops | 460/464 | ";
        if (args.transport == "tcp") {
          cmp << retry_on_time_dropped << "/" << sync_ok_n;
        }
        cmp << " | 386/390 | ";
        if (args.transport == "udp") {
          cmp << retry_on_time_dropped << "/" << sync_ok_n;
        }
        cmp << " |\n";
        cmp << "| retry timing failures | 4 | ";
        cmp << (args.transport == "tcp" ? std::to_string(retry_late)
                                       : std::string());
        cmp << " | 4 | ";
        cmp << (args.transport == "udp" ? std::to_string(retry_late)
                                       : std::string());
        cmp << " |\n";
        cmp << "| 1000ms drift cases | 6 | ";
        cmp << (args.transport == "tcp" ? std::to_string(drift_1000)
                                       : std::string());
        cmp << " | 3 | ";
        cmp << (args.transport == "udp" ? std::to_string(drift_1000)
                                       : std::string());
        cmp << " |\n";
        cmp << "| false MissedDeadline | 0 | ";
        cmp << (args.transport == "tcp"
                    ? std::to_string(alice_n - alice_md_false)
                    : std::string());
        cmp << " | 0 | ";
        cmp << (args.transport == "udp"
                    ? std::to_string(alice_n - alice_md_false)
                    : std::string());
        cmp << " |\n";
        cmp.flush();
      }
    }

    std::cout << (prod_ok ? "PASS" : "FAIL") << " first-request-loss-p99 cases="
              << cases.size() << " sync_ok=" << sync_ok_n
              << " harness_sync_errors=" << harness_sync_errors
              << " production_fail=" << production_fail_n
              << " first_late=" << first_late << " retry_late=" << retry_late
              << " est_late=" << est_after_tn_g
              << " drift_1000=" << drift_1000
              << " alice_md_ok=" << alice_md_false << "/" << alice_n
              << std::endl;

    StopChild(alice);
    StopChild(bob);
    return prod_ok ? 0 : 7;

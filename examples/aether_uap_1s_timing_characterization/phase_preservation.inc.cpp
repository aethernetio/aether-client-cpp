    // Phase-preservation mode. Included inside RunCharacterization after
    // Alice/Bob spawn, warmup, and wait_ping/arm_next lambdas.
    // Terminology mapping:
    //   Peer/Bob = child B (PingCloudServers / ApplyLogicalPingAttempt)
    //   Observer/Alice = child A (Client::QueryPeerReceiveSchedule)
    //   Server = work-cloud AuthorizedApi::ping destination
    //   Nominal ping = attempt_index 1 of a logical cycle (cycle_anchor = Tn)
    //   Retry ping = attempt_index >= 2
    //   Original deadline Tn = PingTraceEvent.cycle_anchor / nominal_ping_at
    //   Corrected window = required_rx_until / effective_wire_rx_window after retry
    //   Nominal phase = T0 + n * 1000ms, never reset from retry completion
    struct PhaseStep {
      const char* sequence;
      const char* fault_type;
      int fault_mode;
      std::int64_t server_offset_us;
      bool use_hold;
      bool checkpoints;
    };
    struct ObserverHit {
      int checkpoint{0};
      std::int64_t query_steady_us{0};
      std::int64_t query_qpc{0};
      std::int64_t rel_deadline_us{0};
      std::int64_t state{-2};
      std::int64_t next_us{0};
      std::int64_t last_online_us{0};
      std::int64_t next_ping_delta_ms{std::numeric_limits<std::int64_t>::min()};
      std::int64_t last_connect_delta_ms{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t expected_state{0};
      bool mismatch{false};
    };
    struct PhaseCycle {
      int index{0};
      const char* sequence{"?"};
      const char* fault_type{"none"};
      int fault_mode{0};
      std::int64_t logical_ping_id{0};
      std::int64_t seed{0};
      std::int64_t phase_anchor_us{0};
      std::int64_t expected_nominal_us{0};
      std::int64_t scheduled_nominal_us{0};
      std::int64_t original_deadline_us{0};
      std::int64_t original_window_start_us{0};
      std::int64_t original_window_end_us{0};
      std::int64_t original_window_dur_us{0};
      std::int64_t computed_guard_us{0};
      std::int64_t scheduled_first_us{0};
      std::int64_t actual_first_send_us{0};
      std::int64_t first_attempt_qpc{0};
      std::int64_t first_request_sent{-1};
      std::int64_t first_server_receive_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_decision_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_scheduled_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_send_us{std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_qpc{0};
      std::int64_t retry_request_sent{-1};
      std::int64_t retry_server_receive_mapped_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t retry_server_receive_observer_us{
          std::numeric_limits<std::int64_t>::min()};
      std::int64_t original_deadline_qpc{0};
      std::int64_t retry_mapped_qpc{0};
      double retry_server_margin_ms{
          std::numeric_limits<double>::quiet_NaN()};
      bool retry_before_deadline{false};
      bool retry_after_deadline{false};
      bool no_retry{false};
      std::int64_t corrected_cur_start_us{0};
      std::int64_t corrected_cur_end_us{0};
      std::int64_t corrected_cur_dur_us{0};
      std::int64_t corrected_next_start_us{0};
      std::int64_t next_scheduled_nominal_us{0};
      std::int64_t accepted_attempt{1};
      int confirms{0};
      bool confirmed{false};
      std::vector<ObserverHit> observers;
      std::vector<std::string> failures;
    };
    struct FailedCase {
      PhaseCycle c;
      std::string invariant;
    };

    auto abs_i64 = [](std::int64_t v) -> std::int64_t {
      return v < 0 ? -v : v;
    };
    auto const kMissing = std::numeric_limits<std::int64_t>::min();
    auto csv_i = [&](std::int64_t v) -> std::string {
      if (v == kMissing) {
        return {};
      }
      return std::to_string(v);
    };
    auto csv_d = [&](double v) -> std::string {
      if (!std::isfinite(v)) {
        return {};
      }
      return F3(v);
    };
    auto json_i = [&](std::int64_t v) -> std::string {
      if (v == kMissing) {
        return "null";
      }
      return std::to_string(v);
    };
    auto json_d = [&](double v) -> std::string {
      if (!std::isfinite(v)) {
        return "null";
      }
      return F3(v);
    };

    LARGE_INTEGER qfreq{};
    QueryPerformanceFrequency(&qfreq);
    double const qpc_per_us =
        static_cast<double>(qfreq.QuadPart) / 1000000.0;
    auto map_us_to_qpc = [&](std::int64_t event_qpc, std::int64_t event_us,
                             std::int64_t target_us) -> std::int64_t {
      return event_qpc + static_cast<std::int64_t>(
                             static_cast<double>(target_us - event_us) *
                             qpc_per_us);
    };
    std::int64_t const one_way_us =
        (warmup_min > 0 ? warmup_min : 100) * 1000 / 2;
    std::int64_t const period_us = args.ping_interval_ms * 1000;
    auto const tick_us = static_cast<std::int64_t>(1000);  // 1ms scheduler tick

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
    auto wait_ev = find_ev;
    auto wait_first_attempt = [&](DWORD timeout_ms)
        -> std::optional<BobPingEvent> {
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
    auto query_ckpt = [&](int ckpt, bool force) -> std::optional<ScheduleSnap> {
      SendRaw(alice, kIpcQueryNow, 0, ckpt, 0, force ? 1 : 0);
      return wait_sched(800);
    };
    auto rel_deadline_us = [&](ScheduleSnap const& q,
                               PhaseCycle const& rec) -> std::int64_t {
      if (rec.original_deadline_qpc == 0 || q.qpc == 0) {
        return 0;
      }
      return static_cast<std::int64_t>(
          static_cast<double>(q.qpc - rec.original_deadline_qpc) / qpc_per_us);
    };
    auto confirm_count = [&](std::int64_t cycle_id) {
      int n = 0;
      for (auto const& e : bob.ping_events) {
        if (!dest_ok(e)) {
          continue;
        }
        if (e.kind ==
                static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed) &&
            e.logical_cycle_id == cycle_id) {
          ++n;
        }
      }
      return n;
    };
    auto apply_retry = [&](PhaseCycle& rec, BobPingEvent const& retry) {
      rec.retry_send_us =
          retry.actual_us != 0 ? retry.actual_us : retry.event_steady_us;
      rec.retry_qpc = retry.event_qpc;
      rec.retry_request_sent = retry.request_was_sent;
      rec.accepted_attempt = retry.physical_attempt_index;
      rec.corrected_cur_start_us = retry.actual_us;
      rec.corrected_cur_end_us = retry.required_until_us;
      rec.corrected_cur_dur_us = retry.effective_window_us;
      rec.corrected_next_start_us = retry.next_local_send_us;
      rec.next_scheduled_nominal_us = retry.contract_deadline_us;
      rec.retry_mapped_qpc =
          retry.event_qpc +
          static_cast<std::int64_t>(static_cast<double>(one_way_us) *
                                    qpc_per_us);
      rec.retry_server_receive_mapped_us = rec.retry_send_us + one_way_us;
      if (rec.retry_mapped_qpc < rec.original_deadline_qpc) {
        rec.retry_before_deadline = true;
        rec.retry_after_deadline = false;
        rec.retry_server_margin_ms = QpcToMs(static_cast<std::uint64_t>(
            rec.original_deadline_qpc - rec.retry_mapped_qpc));
      } else {
        rec.retry_before_deadline = false;
        rec.retry_after_deadline = true;
        rec.retry_server_margin_ms = -QpcToMs(static_cast<std::uint64_t>(
            rec.retry_mapped_qpc - rec.original_deadline_qpc));
      }
    };

    std::vector<PhaseStep> steps;
    auto add_n = [&](char const* seq, char const* ft, int mode, int n,
                     bool ckpt) {
      for (int i = 0; i < n; ++i) {
        steps.push_back(PhaseStep{seq, ft, mode, 0, false, ckpt});
      }
    };
    bool const stress = args.phase_preservation_stress;
    int const budget_sec = args.phase_preservation_budget_sec;
    bool const timed_shard = budget_sec > 0;
    ULONGLONG const shard_start_tick = GetTickCount64();
    ULONGLONG const shard_budget_ms =
        timed_shard ? static_cast<ULONGLONG>(budget_sec) * 1000ULL : 0;
    // Reserve ~120s of a timed shard for in-flight/hard-stop/graceful controls.
    ULONGLONG const main_budget_ms =
        timed_shard && shard_budget_ms > 120000ULL ? shard_budget_ms - 120000ULL
                                                  : shard_budget_ms;
    auto budget_remaining = [&]() -> bool {
      if (!timed_shard) {
        return true;
      }
      return (GetTickCount64() - shard_start_tick) < main_budget_ms;
    };
    auto shard_time_remaining = [&]() -> bool {
      if (!timed_shard) {
        return true;
      }
      return (GetTickCount64() - shard_start_tick) < shard_budget_ms;
    };
    if (timed_shard) {
      // Approximate wall-time mix for long characterization shards.
      // Sparse checkpoints: QueryNow RTT must not dominate the 1s loop.
      auto sparse = [](int i) { return (i % 8) == 0; };
      add_n("baseline", "none", 0, 250, false);
      for (int i = 0; i < 250; ++i) {
        steps.push_back(PhaseStep{"request-loss", "request-loss", 1, 0, false,
                                  sparse(i)});
      }
      for (int i = 0; i < 200; ++i) {
        steps.push_back(PhaseStep{"response-loss", "response-loss", 2, 0, false,
                                  sparse(i)});
      }
      for (int n : {2, 5, 10}) {
        add_n("consecutive-request-loss", "request-loss", 1, n, sparse(0));
        add_n("consecutive-response-loss", "response-loss", 2, n, sparse(0));
      }
      for (int n : {10, 20, 50}) {
        for (int i = 0; i < n; ++i) {
          bool const req = (i % 2) == 0;
          steps.push_back(PhaseStep{
              "alternating", req ? "request-loss" : "response-loss",
              req ? 1 : 2, 0, false, sparse(i)});
        }
      }
      for (int every : {2, 3, 5, 10}) {
        for (int i = 0; i < every * 20; ++i) {
          bool const loss = (i % every) == 0;
          char const* ft = loss ? ((every % 2) == 0 ? "request-loss"
                                                    : "response-loss")
                                : "none";
          int mode = loss ? ((every % 2) == 0 ? 1 : 2) : 0;
          steps.push_back(PhaseStep{"periodic", ft, mode, 0, false,
                                    loss && sparse(i)});
        }
      }
      for (int pct : {1, 5, 10, 20}) {
        std::uint32_t rr = args.seed ^ static_cast<std::uint32_t>(pct * 0x9e37);
        for (int i = 0; i < 200; ++i) {
          rr = rr * 1664525u + 1013904223u;
          bool const loss = (rr % 100u) < static_cast<std::uint32_t>(pct);
          bool const req = (rr & 1u) == 0;
          char const* ft =
              !loss ? "none" : (req ? "request-loss" : "response-loss");
          int mode = !loss ? 0 : (req ? 1 : 2);
          steps.push_back(PhaseStep{"random", ft, mode, 0, false,
                                    loss && sparse(i)});
        }
      }
      // Repeat the block so wall-clock budget, not step count, ends the shard.
      auto const block = steps;
      while (steps.size() < 20000) {
        steps.insert(steps.end(), block.begin(), block.end());
      }
    } else {
      int const n_base = stress ? 2000 : 100;
      int const n_req = stress ? 500 : 30;
      int const n_resp = stress ? 500 : 30;
      add_n("baseline", "none", 0, n_base, false);
      add_n("request-loss", "request-loss", 1, n_req, true);
      add_n("response-loss", "response-loss", 2, n_resp, true);
      if (!stress) {
        std::int64_t const before[] = {-100000, -50000, -20000, -10000, -5000,
                                       -2000, -1000};
        std::int64_t const after[] = {1000, 2000, 5000, 10000};
        for (auto off : before) {
          steps.push_back(PhaseStep{"boundary-before", "request-loss", 1, off,
                                    true, true});
        }
        for (auto off : after) {
          steps.push_back(PhaseStep{"boundary-after", "request-loss", 1, off,
                                    true, true});
        }
        add_n("consecutive-request-loss", "request-loss", 1, 10, true);
        add_n("consecutive-response-loss", "response-loss", 2, 10, true);
        for (int i = 0; i < 20; ++i) {
          steps.push_back(
              PhaseStep{"alternating",
                        (i % 2) == 0 ? "request-loss" : "response-loss",
                        (i % 2) == 0 ? 1 : 2, 0, false, true});
        }
        for (int i = 0; i < 20; ++i) {
          bool const loss = (i % 2) == 0;
          steps.push_back(PhaseStep{"every-2", loss ? "request-loss" : "none",
                                    loss ? 1 : 0, 0, false, loss});
        }
        for (int i = 0; i < 25; ++i) {
          bool const loss = (i % 5) == 0;
          steps.push_back(PhaseStep{"every-5", loss ? "response-loss" : "none",
                                    loss ? 2 : 0, 0, false, loss});
        }
      } else {
        for (int s = 0; s < 100; ++s) {
          steps.push_back(PhaseStep{"burst-request", "request-loss", 1, 0,
                                    false, s % 10 == 0});
          steps.push_back(PhaseStep{"burst-response", "response-loss", 2, 0,
                                    false, s % 10 == 0});
        }
        std::uint32_t rr = args.seed ^ 0x9e3779b9u;
        for (int i = 0; i < 1000; ++i) {
          rr = rr * 1664525u + 1013904223u;
          int mode = 0;
          char const* ft = "none";
          if ((rr % 5u) == 0) {
            mode = 1;
            ft = "request-loss";
          } else if ((rr % 5u) == 1) {
            mode = 2;
            ft = "response-loss";
          }
          steps.push_back(PhaseStep{"random-1000", ft, mode, 0, false, false});
        }
      }
    }

    std::filesystem::create_directories(args.artifact_dir);
    std::ofstream jsonl(std::filesystem::path{args.artifact_dir} /
                        "samples.jsonl");
    std::ofstream samples_csv(std::filesystem::path{args.artifact_dir} /
                              "samples.csv");
    std::ofstream phase_csv(std::filesystem::path{args.artifact_dir} /
                            "phase-error-by-cycle.csv");
    std::ofstream win_csv(std::filesystem::path{args.artifact_dir} /
                          "window-corrections.csv");
    std::ofstream obs_csv(std::filesystem::path{args.artifact_dir} /
                          "observer-query-results.csv");
    if (!jsonl || !samples_csv || !phase_csv || !win_csv || !obs_csv) {
      std::cerr << "FAIL cannot open phase-preservation csv/jsonl outputs\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }
    samples_csv
        << "run_id,transport,shard,seed,cycle_index,logical_ping_id,sequence,"
           "fault_type,fault_armed,fault_consumed,attempt_number,Tn,Tn_plus_1,"
           "scheduled_nominal,scheduled_phase_error_ms,guard_ms,"
           "attempt_lead_ms,first_attempt_scheduled,first_attempt_actual_send,"
           "first_attempt_offset_from_Tn_ms,first_request_sent,"
           "estimated_first_server_receive,attempt_timeout,retry_decision,"
           "retry_actual_send,timeout_to_retry_decision_ms,"
           "timeout_to_retry_send_ms,first_attempt_to_retry_ms,"
           "retry_send_offset_from_Tn_ms,retry_client_margin_to_Tn_ms,"
           "estimated_server_receive,estimated_server_margin_ms,"
           "one_way_estimate_us,current_window_before,current_window_after,"
           "next_scheduled_nominal,next_nominal_phase_error_ms,"
           "alice_state,alice_next_ping_delta_ms,duplicate_count,"
           "cycle_confirmed,failure_class\n";
    phase_csv << "cycle_index,sequence,fault_type,expected_nominal_us,"
                 "scheduled_nominal_us,scheduled_phase_error_us,actual_first_"
                 "send_us,actual_send_phase_error_us,actual_interval_error_us,"
                 "contiguous_cycle,skipped_slots,next_window_phase_error_us\n";
    win_csv << "cycle_index,sequence,fault_type,original_window_start_us,"
               "original_window_end_us,original_window_duration_us,corrected_"
               "current_window_start_us,corrected_current_window_end_us,"
               "corrected_current_window_duration_us,current_window_start_"
               "delta_ms,current_window_end_delta_ms,current_window_duration_"
               "delta_ms,next_window_start_delta_ms,next_nominal_phase_delta_"
               "ms\n";
    obs_csv << "cycle_index,sequence,fault_type,checkpoint,query_time_us,"
               "rel_deadline_us,state,expected_state,next_us,last_online_us,"
               "next_ping_delta_ms,last_connect_delta_ms,mismatch,"
               "alice_query_rtt_ms\n";

    std::ofstream meta_json(std::filesystem::path{args.artifact_dir} /
                            "shard-meta.json");
    if (meta_json) {
      meta_json << "{\"run_id\":\"" << args.run_id << "\",\"transport\":\""
                << args.transport << "\",\"seed\":" << args.seed
                << ",\"budget_sec\":" << budget_sec
                << ",\"one_way_estimate_us\":" << one_way_us
                << ",\"warmup_min_rtt_ms\":" << warmup_min
                << ",\"warmup_p99_rtt_ms\":" << warmup_p99 << "}\n";
      meta_json.flush();
    }

    std::vector<PhaseCycle> cycles;
    std::vector<FailedCase> failed;
    std::int64_t phase_anchor_us = 0;
    std::int64_t prev_first_send_us = 0;
    std::int64_t prev_tn1_us = 0;
    std::int64_t prev_scheduled_us = 0;
    int live_false_md = 0;
    int live_false_unknown = 0;
    int duplicate_logical = 0;
    int query_failures = 0;
    bool ok_phase = true;
    int graceful_hit = 0;
    int hard_hit = 0;

    std::cout << "PHASE_PRESERVATION mode="
              << (timed_shard ? "budget-shard"
                              : (stress ? "stress" : "fast"))
              << " steps=" << steps.size() << " seed=" << args.seed
              << " budget_sec=" << budget_sec << std::endl;
    wait_window_closed(8000);

    auto fail_inv = [&](PhaseCycle& rec, char const* inv) {
      rec.failures.push_back(inv);
      FailedCase fc{};
      fc.c = rec;
      fc.invariant = inv;
      failed.push_back(fc);
      ok_phase = false;
      std::cerr << "FAIL cycle " << rec.index << " " << rec.sequence << " "
                << rec.fault_type << ": " << inv << std::endl;
    };

    int armed_si = -1;
    auto arm_step = [&](PhaseStep const& step, int index) {
      if (step.use_hold) {
        auto const send_hold_us = step.server_offset_us - one_way_us;
        SendRaw(bob, kIpcArmFault, 0, dest, 1, step.fault_mode, 20000, 0, 0);
        drain(40);
        SendRaw(bob, kIpcArmFault, 0, dest, 2, 0, 0, 1, 0, send_hold_us, 1);
        drain(40);
      } else if (step.fault_mode != 0) {
        arm_next(step.fault_mode, 1, 0, 0);
      } else {
        SendRaw(bob, kIpcArmFault, 0, dest, 1, 0, 0, 0, 0);
        drain(20);
      }
      armed_si = index;
    };

    for (int si = 0; si < static_cast<int>(steps.size()); ++si) {
      if (!budget_remaining()) {
        std::cout << "PHASE_PRESERVATION budget exhausted after " << si
                  << " steps elapsed_ms="
                  << (GetTickCount64() - shard_start_tick) << std::endl;
        break;
      }
      auto const& st = steps[static_cast<std::size_t>(si)];
      PhaseCycle rec{};
      rec.index = si;
      rec.sequence = st.sequence;
      rec.fault_type = st.fault_type;
      rec.fault_mode = st.fault_mode;
      rec.seed = args.seed;
      std::optional<BobPingEvent> started;
      int const arm_tries = st.use_hold ? 1 : 4;
      for (int arm_try = 0; arm_try < arm_tries; ++arm_try) {
        if (armed_si != si || arm_try > 0) {
          arm_step(st, si);
        }
        started = wait_first_attempt(8000);
        if (!started) {
          break;
        }
        if (st.use_hold) {
          break;
        }
        bool const drop_missed =
            st.fault_mode == 1 && started->request_was_sent == 1;
        bool const ignore_missed =
            st.fault_mode == 2 && started->request_was_sent == 0;
        bool ignore_not_applied = false;
        if (st.fault_mode == 2 && started->request_was_sent == 1) {
          auto to = find_ev(
              static_cast<std::uint8_t>(PingTraceKind::kAttemptTimeout), 400,
              started->logical_cycle_id, 0);
          ignore_not_applied = !to;
          if (to) {
            rec.retry_decision_us = to->event_steady_us;
          }
        }
        if (!drop_missed && !ignore_missed && !ignore_not_applied) {
          break;
        }
        (void)find_ev(static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed),
                      2000, started->logical_cycle_id, 0);
        armed_si = -1;
        if (arm_try + 1 < arm_tries) {
          started.reset();
        }
      }
      if (!started) {
        fail_inv(rec, "reporting/harness failure: no cycle start trace");
        cycles.push_back(std::move(rec));
        continue;
      }
      rec.logical_ping_id = started->logical_cycle_id;
      rec.scheduled_nominal_us = started->cycle_anchor_us;
      rec.original_deadline_us = started->cycle_anchor_us;
      rec.next_scheduled_nominal_us = started->contract_deadline_us;
      rec.actual_first_send_us = started->actual_us != 0 ? started->actual_us
                                                         : started->event_steady_us;
      rec.first_attempt_qpc = started->event_qpc;
      rec.scheduled_first_us = started->planned_us;
      rec.computed_guard_us = started->guard_us;
      rec.original_window_dur_us = started->base_window_us;
      rec.original_window_start_us = started->cycle_anchor_us;
      rec.original_window_end_us =
          started->cycle_anchor_us + started->base_window_us;
      rec.first_request_sent = started->request_was_sent;
      rec.corrected_cur_start_us = started->actual_us;
      rec.corrected_cur_end_us = started->required_until_us;
      rec.corrected_cur_dur_us = started->effective_window_us;
      rec.corrected_next_start_us = started->next_local_send_us;
      if (phase_anchor_us == 0) {
        phase_anchor_us = rec.scheduled_nominal_us;
      }
      rec.phase_anchor_us = phase_anchor_us;
      {
        auto const delta = rec.scheduled_nominal_us - phase_anchor_us;
        auto n = delta / period_us;
        auto rem = delta % period_us;
        if (rem < 0) {
          rem += period_us;
          n -= 1;
        }
        if (rem > period_us / 2) {
          n += 1;
        }
        rec.expected_nominal_us = phase_anchor_us + n * period_us;
      }
      rec.original_deadline_qpc =
          map_us_to_qpc(started->event_qpc, rec.actual_first_send_us,
                        rec.original_deadline_us);

      if (st.checkpoints) {
        auto q1 = query_ckpt(1, false);
        if (!q1) {
          ++query_failures;
          fail_inv(rec, "observer checkpoint 1: QueryPeerPresence did not complete");
        } else {
          ObserverHit h{};
          h.checkpoint = 1;
          h.query_steady_us = q1->steady_us;
          h.query_qpc = q1->qpc;
          h.state = q1->state;
          h.next_us = q1->next_us;
          h.last_online_us = q1->last_online_us;
          h.next_ping_delta_ms = q1->next_ping_delta_ms;
          h.last_connect_delta_ms = q1->last_connect_delta_ms;
          h.rel_deadline_us = rel_deadline_us(*q1, rec);
          h.expected_state = 0;
          if (q1->state == 1) {
            ++live_false_md;
            h.mismatch = true;
            fail_inv(rec, "observer saw false MissedDeadline before first attempt");
          } else if (q1->state == 2) {
            ++live_false_unknown;
            h.mismatch = true;
            fail_inv(rec, "observer saw false Unknown before first attempt");
          } else if (q1->state != 0) {
            h.mismatch = true;
            fail_inv(rec, "observer saw the wrong state at checkpoint 1");
          }
          rec.observers.push_back(h);
        }
      }

      if (st.fault_mode == 1 && rec.first_request_sent == 1) {
        fail_inv(rec, "request-loss first request was sent to the server");
      }
      if (st.fault_mode == 2 && rec.first_request_sent == 0) {
        fail_inv(rec, "response-loss first request did not reach send");
      }
      if (rec.first_request_sent == 1) {
        rec.first_server_receive_us = rec.actual_first_send_us + one_way_us;
      }

      std::optional<BobPingEvent> confirmed;
      if (st.fault_mode != 0) {
        bool const expect_retry = rec.first_request_sent ==
                                  (st.fault_mode == 1 ? 0 : 1);
        DWORD const retry_wait_ms =
            st.use_hold ? 6000 : (expect_retry ? 1500 : 200);
        if (rec.retry_decision_us == kMissing) {
          auto timeout_ev = find_ev(
              static_cast<std::uint8_t>(PingTraceKind::kAttemptTimeout),
              retry_wait_ms, rec.logical_ping_id, 0);
          if (timeout_ev) {
            rec.retry_decision_us = timeout_ev->event_steady_us;
          }
        }
        if (rec.retry_decision_us != kMissing) {
          auto retry_sched = find_ev(
              static_cast<std::uint8_t>(PingTraceKind::kRetryScheduled), 200,
              rec.logical_ping_id, 0);
          if (retry_sched) {
            rec.retry_scheduled_us = retry_sched->event_steady_us;
          }
          if (st.checkpoints) {
            auto q2 = query_ckpt(2, false);
            if (q2) {
              ObserverHit h{};
              h.checkpoint = 2;
              h.query_steady_us = q2->steady_us;
              h.query_qpc = q2->qpc;
              h.state = q2->state;
              h.next_us = q2->next_us;
              h.last_online_us = q2->last_online_us;
              h.next_ping_delta_ms = q2->next_ping_delta_ms;
              h.last_connect_delta_ms = q2->last_connect_delta_ms;
              h.rel_deadline_us = rel_deadline_us(*q2, rec);
              h.expected_state = 0;
              if (q2->state == 1) {
                ++live_false_md;
                h.mismatch = true;
                fail_inv(rec,
                         "observer saw false MissedDeadline after loss before retry");
              }
              rec.observers.push_back(h);
            } else {
              ++query_failures;
            }
          }
        }
        auto retry = find_ev(
            static_cast<std::uint8_t>(PingTraceKind::kRequestSent),
            st.use_hold ? 5000 : 800, rec.logical_ping_id, 2);
        if (!retry) {
          retry = find_ev(
              static_cast<std::uint8_t>(PingTraceKind::kRequestDropped), 200,
              rec.logical_ping_id, 2);
        }
        if (!retry) {
          rec.no_retry = true;
          fail_inv(rec, "retry did not reach server");
        } else {
          apply_retry(rec, *retry);
          if (retry->request_was_sent == 0) {
            rec.no_retry = true;
            fail_inv(rec, "retry did not reach server");
          }
        }
        if (si + 1 < static_cast<int>(steps.size())) {
          arm_step(steps[static_cast<std::size_t>(si + 1)], si + 1);
        }
        if (st.checkpoints && !rec.no_retry) {
          auto q3 = query_ckpt(3, false);
          if (q3) {
            ObserverHit h{};
            h.checkpoint = 3;
            h.query_steady_us = q3->steady_us;
            h.query_qpc = q3->qpc;
            h.state = q3->state;
            h.next_us = q3->next_us;
            h.last_online_us = q3->last_online_us;
            h.next_ping_delta_ms = q3->next_ping_delta_ms;
            h.last_connect_delta_ms = q3->last_connect_delta_ms;
            h.rel_deadline_us = rel_deadline_us(*q3, rec);
            h.expected_state = 0;
            rec.retry_server_receive_observer_us = q3->last_online_us;
            if (q3->state == 1 && rec.retry_before_deadline) {
              ++live_false_md;
              h.mismatch = true;
              fail_inv(rec, "observer saw false MissedDeadline after retry reached server before original deadline");
            }
            rec.observers.push_back(h);
          }
          if (rec.original_deadline_qpc > 0) {
            auto const now_qpc = QpcNow();
            if (now_qpc < rec.original_deadline_qpc) {
              auto const wait_ms = QpcToMs(static_cast<std::uint64_t>(
                  rec.original_deadline_qpc - now_qpc));
              if (wait_ms > 0 && wait_ms < 800) {
                Sleep(static_cast<DWORD>(wait_ms + 2));
              }
            }
          }
          auto q4 = query_ckpt(4, false);
          if (q4) {
            ObserverHit h{};
            h.checkpoint = 4;
            h.query_steady_us = q4->steady_us;
            h.query_qpc = q4->qpc;
            h.state = q4->state;
            h.next_us = q4->next_us;
            h.last_online_us = q4->last_online_us;
            h.next_ping_delta_ms = q4->next_ping_delta_ms;
            h.last_connect_delta_ms = q4->last_connect_delta_ms;
            h.rel_deadline_us = rel_deadline_us(*q4, rec);
            h.expected_state = rec.retry_after_deadline ? q4->state : 0;
            if (rec.retry_before_deadline && q4->state == 1) {
              ++live_false_md;
              h.mismatch = true;
              fail_inv(rec, "observer saw false MissedDeadline after original deadline following before-deadline retry");
            }
            rec.observers.push_back(h);
          }
        }
      } else if (si + 1 < static_cast<int>(steps.size())) {
        arm_step(steps[static_cast<std::size_t>(si + 1)], si + 1);
      }

      if (!confirmed) {
        confirmed = wait_ev(
            static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 8000,
            rec.logical_ping_id, 0);
      }
      if (confirmed) {
        rec.confirmed = true;
        rec.confirms = 1;
      } else {
        fail_inv(rec, "reporting/harness failure: cycle not confirmed");
      }
      rec.confirms = confirm_count(rec.logical_ping_id);
      if (rec.confirms > 1) {
        duplicate_logical += rec.confirms - 1;
        fail_inv(rec, "duplicate logical ping / duplicate CycleConfirmed");
      }

      if (st.use_hold && rec.retry_before_deadline == false &&
          st.server_offset_us < 0 && !rec.no_retry) {
        fail_inv(rec, "retry reached server after original deadline");
      }
      if (st.use_hold && rec.retry_after_deadline == false &&
          st.server_offset_us > 0 && !rec.no_retry) {
        fail_inv(rec, "retry reached server after original deadline was expected but classified as before-deadline");
      }
      if (!st.use_hold && st.fault_mode != 0 && rec.retry_after_deadline) {
        fail_inv(rec, "retry reached server after original deadline");
      }

      auto const sched_err =
          rec.scheduled_nominal_us - rec.expected_nominal_us;
      if (abs_i64(sched_err) > tick_us) {
        fail_inv(rec, "next nominal schedule shifted");
      }
      if (prev_tn1_us != 0 && rec.scheduled_nominal_us != 0 &&
          abs_i64(rec.scheduled_nominal_us - prev_scheduled_us - period_us) <=
              tick_us &&
          abs_i64(rec.scheduled_nominal_us - prev_tn1_us) > tick_us) {
        fail_inv(rec, "next nominal schedule shifted");
      }
      if (st.checkpoints) {
        auto q5 = query_ckpt(5, false);
        if (q5) {
          ObserverHit h{};
          h.checkpoint = 5;
          h.query_steady_us = q5->steady_us;
          h.query_qpc = q5->qpc;
          h.state = q5->state;
          h.next_us = q5->next_us;
          h.last_online_us = q5->last_online_us;
          h.next_ping_delta_ms = q5->next_ping_delta_ms;
          h.last_connect_delta_ms = q5->last_connect_delta_ms;
          h.rel_deadline_us = rel_deadline_us(*q5, rec);
          h.expected_state = 0;
          if (q5->state == 1) {
            ++live_false_md;
            h.mismatch = true;
            fail_inv(rec, "observer saw false MissedDeadline after the next nominal ping");
          } else if (q5->state == 2) {
            ++live_false_unknown;
            h.mismatch = true;
            fail_inv(rec, "observer saw false Unknown after the next nominal ping");
          }
          rec.observers.push_back(h);
        }
      }

      std::int64_t interval_err = kMissing;
      int contiguous = 0;
      std::int64_t skipped_slots = kMissing;
      if (prev_first_send_us != 0 && rec.actual_first_send_us != 0) {
        auto const raw_interval =
            rec.actual_first_send_us - prev_first_send_us;
        auto slots = raw_interval / period_us;
        auto rem = raw_interval % period_us;
        if (rem < 0) {
          rem += period_us;
          slots -= 1;
        }
        if (rem > period_us / 2) {
          slots += 1;
        }
        if (slots <= 1) {
          contiguous = 1;
          interval_err = raw_interval - period_us;
          skipped_slots = 0;
        } else {
          contiguous = 0;
          interval_err = raw_interval - period_us;
          skipped_slots = slots - 1;
        }
      }
      auto const send_phase_err =
          rec.actual_first_send_us - rec.expected_nominal_us;
      auto const next_phase_err =
          rec.next_scheduled_nominal_us -
          (rec.expected_nominal_us + period_us);
      auto const sched_err_ms = sched_err / 1000.0;
      auto const next_phase_err_ms = next_phase_err / 1000.0;
      auto const first_off_ms =
          (rec.actual_first_send_us - rec.scheduled_nominal_us) / 1000.0;
      auto const attempt_lead_ms =
          started->attempt_lead_us > 0
              ? started->attempt_lead_us / 1000.0
              : (rec.scheduled_nominal_us - rec.scheduled_first_us) / 1000.0;
      auto const guard_ms = rec.computed_guard_us / 1000.0;
      int fault_armed = st.fault_mode != 0 ? 1 : 0;
      int fault_consumed = 0;
      if (st.fault_mode == 1) {
        fault_consumed = rec.first_request_sent == 0 ? 1 : 0;
      } else if (st.fault_mode == 2) {
        fault_consumed = rec.first_request_sent == 1 ? 1 : 0;
      }
      std::int64_t timeout_to_decision = kMissing;
      std::int64_t timeout_to_retry = kMissing;
      std::int64_t first_to_retry = kMissing;
      double retry_send_off_ms = std::numeric_limits<double>::quiet_NaN();
      double retry_client_margin_ms = std::numeric_limits<double>::quiet_NaN();
      if (rec.retry_decision_us != kMissing &&
          rec.retry_send_us != kMissing) {
        // retry_decision is attempt timeout time when available
      }
      if (rec.retry_decision_us != kMissing) {
        timeout_to_decision = 0;
      }
      if (rec.retry_decision_us != kMissing &&
          rec.retry_send_us != kMissing) {
        timeout_to_retry = rec.retry_send_us - rec.retry_decision_us;
      }
      if (rec.retry_send_us != kMissing && rec.actual_first_send_us != 0) {
        first_to_retry = rec.retry_send_us - rec.actual_first_send_us;
        retry_send_off_ms =
            (rec.retry_send_us - rec.scheduled_nominal_us) / 1000.0;
        retry_client_margin_ms =
            (rec.scheduled_nominal_us - rec.retry_send_us) / 1000.0;
      }
      std::string failure_class;
      if (!rec.failures.empty()) {
        auto const& inv = rec.failures.front();
        if (inv.find("next nominal schedule shifted") != std::string::npos) {
          failure_class = "PRODUCTION_PHASE";
        } else if (inv.find("retry reached server after") != std::string::npos) {
          failure_class = "PRODUCTION_RETRY_ESTIMATED_LATE_ARRIVAL";
        } else if (inv.find("first request was sent") != std::string::npos ||
                   inv.find("did not reach send") != std::string::npos) {
          failure_class = "HARNESS_FAULT_NOT_ARMED";
        } else if (inv.find("cycle not confirmed") != std::string::npos ||
                   inv.find("no cycle start") != std::string::npos) {
          failure_class = "HARNESS_CYCLE_CONFIRM";
        } else if (inv.find("QueryPeerReceiveSchedule") != std::string::npos ||
                   inv.find("observer") != std::string::npos) {
          failure_class = "HARNESS_QUERY";
        } else if (inv.find("retry did not reach") != std::string::npos) {
          failure_class = "HARNESS_FAULT_WRONG_CYCLE";
        } else {
          failure_class = "OTHER";
        }
      }
      std::int64_t alice_state = kMissing;
      std::int64_t alice_delta = kMissing;
      if (!rec.observers.empty()) {
        alice_state = rec.observers.back().state;
        alice_delta = rec.observers.back().next_ping_delta_ms;
      }

      jsonl << "{\"run_id\":\"" << args.run_id << "\",\"transport\":\""
            << args.transport << "\",\"seed\":" << args.seed
            << ",\"cycle_index\":" << si << ",\"logical_ping_id\":"
            << rec.logical_ping_id << ",\"attempt_number\":"
            << rec.accepted_attempt << ",\"fault_type\":\"" << rec.fault_type
            << "\",\"fault_armed\":" << fault_armed
            << ",\"fault_consumed\":" << fault_consumed
            << ",\"phase_anchor\":" << rec.phase_anchor_us
            << ",\"Tn\":" << rec.scheduled_nominal_us
            << ",\"Tn_plus_1\":" << (rec.scheduled_nominal_us + period_us)
            << ",\"expected_nominal_time\":" << rec.expected_nominal_us
            << ",\"original_deadline\":" << rec.original_deadline_us
            << ",\"actual_first_attempt_send_time\":" << rec.actual_first_send_us
            << ",\"retry_actual_send_time\":" << json_i(rec.retry_send_us)
            << ",\"estimated_server_receive\":"
            << json_i(rec.retry_server_receive_mapped_us)
            << ",\"estimated_server_margin_ms\":"
            << json_d(rec.retry_server_margin_ms)
            << ",\"one_way_estimate_us\":" << one_way_us
            << ",\"next_scheduled_nominal_time\":"
            << rec.next_scheduled_nominal_us
            << ",\"scheduled_phase_error_ms\":" << json_d(sched_err_ms)
            << ",\"next_nominal_phase_error_ms\":" << json_d(next_phase_err_ms)
            << ",\"contiguous_cycle\":" << contiguous
            << ",\"skipped_slots\":" << json_i(skipped_slots)
            << ",\"failure_class\":"
            << (failure_class.empty() ? "null"
                                      : ("\"" + failure_class + "\""))
            << ",\"failures\":[";
      for (std::size_t fi = 0; fi < rec.failures.size(); ++fi) {
        if (fi != 0) {
          jsonl << ",";
        }
        jsonl << "\"" << rec.failures[fi] << "\"";
      }
      jsonl << "]}\n";
      jsonl.flush();

      samples_csv << args.run_id << "," << args.transport << ","
                  << args.run_id << "," << args.seed << "," << si << ","
                  << rec.logical_ping_id << "," << rec.sequence << ","
                  << rec.fault_type << "," << fault_armed << ","
                  << fault_consumed << "," << rec.accepted_attempt << ","
                  << rec.scheduled_nominal_us << ","
                  << (rec.scheduled_nominal_us + period_us) << ","
                  << rec.scheduled_nominal_us << "," << F3(sched_err_ms) << ","
                  << F3(guard_ms) << "," << F3(attempt_lead_ms) << ","
                  << rec.scheduled_first_us << "," << rec.actual_first_send_us
                  << "," << F3(first_off_ms) << ","
                  << csv_i(rec.first_request_sent) << ","
                  << csv_i(rec.first_server_receive_us) << ","
                  << csv_i(rec.retry_decision_us) << ","
                  << csv_i(rec.retry_scheduled_us) << ","
                  << csv_i(rec.retry_send_us) << ","
                  << csv_i(timeout_to_decision) << ","
                  << csv_i(timeout_to_retry) << "," << csv_i(first_to_retry)
                  << "," << csv_d(retry_send_off_ms) << ","
                  << csv_d(retry_client_margin_ms) << ","
                  << csv_i(rec.retry_server_receive_mapped_us) << ","
                  << csv_d(rec.retry_server_margin_ms) << "," << one_way_us
                  << "," << rec.original_window_dur_us << ","
                  << rec.corrected_cur_dur_us << ","
                  << rec.next_scheduled_nominal_us << ","
                  << F3(next_phase_err_ms) << "," << csv_i(alice_state) << ","
                  << csv_i(alice_delta) << ","
                  << (rec.confirms > 1 ? rec.confirms - 1 : 0) << ","
                  << (rec.confirmed ? 1 : 0) << "," << failure_class << "\n";
      samples_csv.flush();
      phase_csv << si << "," << rec.sequence << "," << rec.fault_type << ","
                << rec.expected_nominal_us << "," << rec.scheduled_nominal_us
                << "," << sched_err << "," << rec.actual_first_send_us << ","
                << send_phase_err << "," << csv_i(interval_err) << ","
                << contiguous << "," << csv_i(skipped_slots) << ","
                << next_phase_err << "\n";
      phase_csv.flush();
      auto const cur_start_d =
          (rec.corrected_cur_start_us - rec.original_window_start_us) / 1000.0;
      auto const cur_end_d =
          (rec.corrected_cur_end_us - rec.original_window_end_us) / 1000.0;
      auto const cur_dur_d =
          (rec.corrected_cur_dur_us - rec.original_window_dur_us) / 1000.0;
      auto const next_start_d =
          rec.corrected_next_start_us == 0
              ? 0.0
              : (rec.corrected_next_start_us -
                 (rec.scheduled_nominal_us + period_us -
                  started->attempt_lead_us)) /
                    1000.0;
      win_csv << si << "," << rec.sequence << "," << rec.fault_type << ","
              << rec.original_window_start_us << ","
              << rec.original_window_end_us << "," << rec.original_window_dur_us
              << "," << rec.corrected_cur_start_us << ","
              << rec.corrected_cur_end_us << "," << rec.corrected_cur_dur_us
              << "," << F3(cur_start_d) << "," << F3(cur_end_d) << ","
              << F3(cur_dur_d) << "," << F3(next_start_d) << ","
              << F3(next_phase_err / 1000.0) << "\n";
      win_csv.flush();
      for (auto const& h : rec.observers) {
        double q_rtt = std::numeric_limits<double>::quiet_NaN();
        obs_csv << si << "," << rec.sequence << "," << rec.fault_type << ","
                << h.checkpoint << "," << h.query_steady_us << ","
                << h.rel_deadline_us << "," << h.state << ","
                << h.expected_state << "," << h.next_us << ","
                << h.last_online_us << "," << csv_i(h.next_ping_delta_ms) << ","
                << csv_i(h.last_connect_delta_ms) << ","
                << (h.mismatch ? 1 : 0) << "," << csv_d(q_rtt) << "\n";
      }
      obs_csv.flush();

      prev_first_send_us = rec.actual_first_send_us;
      prev_tn1_us = rec.next_scheduled_nominal_us;
      prev_scheduled_us = rec.scheduled_nominal_us;
      cycles.push_back(std::move(rec));
    }

    // In-flight QueryNow reuse during one request-loss and one response-loss.
    // Timed shards keep controls brief (~5% wall time) and skip if budget gone.
    int inflight_reused = 0;
    int inflight_skipped = 0;
    int inflight_extra = 0;
    bool const run_controls = !timed_shard || shard_time_remaining();
    auto reuse_once = [&](int mode) {
      if (!run_controls) {
        return;
      }
      wait_window_closed(4000);
      arm_next(mode, 1, 0, 0);
      auto first = wait_first_attempt(8000);
      SendRaw(alice, kIpcQueryNow, 0, 1, 0, 0);
      SendRaw(alice, kIpcQueryNow, 0, 1, 0, 1);
      drain(200);
      if (!alice.query_stats.empty()) {
        auto const& q = alice.query_stats.back();
        inflight_reused += static_cast<int>(q.reused);
        inflight_skipped += static_cast<int>(q.skipped);
        inflight_extra += static_cast<int>(q.extra);
      }
      auto const cid = first ? first->logical_cycle_id : 0;
      (void)wait_ev(static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed),
                    8000, cid, 0);
    };
    reuse_once(1);
    reuse_once(2);

    // Hard-stop first while Bob is still advertising a live schedule.
    // AnnounceUnknown first would leave Alice in Unknown after KillChild.
    if (run_controls) {
    wait_window_closed(4000);
    {
      auto live = query_ckpt(0, false);
      double remaining_ms = 0;
      if (live && live->next_ping_delta_ms >
                      std::numeric_limits<std::int64_t>::min() / 2 &&
          live->next_ping_delta_ms > 0) {
        remaining_ms = static_cast<double>(live->next_ping_delta_ms);
      }
      DWORD poll_ms = 20000;
      if (remaining_ms > 0 && remaining_ms < 60000) {
        auto const need = static_cast<DWORD>(remaining_ms + 15000);
        if (need > poll_ms) {
          poll_ms = need;
        }
      }
      if (poll_ms > 45000) {
        poll_ms = 45000;
      }
      std::cout << "hard-stop remaining_ms=" << remaining_ms
                << " poll_ms=" << poll_ms << std::endl;
      KillChild(bob);
      auto const deadline = GetTickCount64() + poll_ms;
      while (GetTickCount64() < deadline) {
        auto q = query_ckpt(0, false);
        if (q && q->state == 1) {
          hard_hit = 1;
          break;
        }
      }
    }
    if (hard_hit != 1) {
      ok_phase = false;
      std::cerr << "FAIL hard-stop control did not yield MissedDeadline/state 1\n";
      FailedCase fc{};
      fc.invariant = "observer saw the wrong state (hard-stop MissedDeadline expected)";
      failed.push_back(fc);
    }

    auto restart_for_graceful = [&]() -> bool {
      if (alice.pi.hProcess != nullptr) {
        StopChild(alice);
      }
      if (bob.pi.hProcess != nullptr) {
        StopChild(bob);
      }
      Sleep(1000);
      ResetChildRuntime(alice);
      ResetChildRuntime(bob);
      alice.side = IpcSide::kA;
      bob.side = IpcSide::kB;
      auto const pipe_a2 = PipeNameFor(args.run_id, IpcSide::kA, "a-phase-g");
      auto const pipe_b2 = PipeNameFor(args.run_id, IpcSide::kB, "b-phase-g");
      auto const log_a =
          (std::filesystem::path{args.artifact_dir} / "alice-graceful.log")
              .string();
      auto const log_b =
          (std::filesystem::path{args.artifact_dir} / "bob-graceful.log")
              .string();
      auto const state_a2 = (state_root / "state-a-phase-g").string();
      auto const state_b2 = (state_root / "state-b-phase-g").string();
      std::filesystem::create_directories(state_a2);
      std::filesystem::create_directories(state_b2);
      if (!SpawnChild(alice, args, state_a2, pipe_a2, "uap-1s-alice", log_a,
                      args.ping_interval_ms, args.receive_window_ms) ||
          !SpawnChild(bob, args, state_b2, pipe_b2, "uap-1s-bob", log_b,
                      args.ping_interval_ms, args.receive_window_ms)) {
        return false;
      }
      ping_cursor = 0;
      if (!wait_ready(alice, "Alice-graceful") ||
          !wait_ready(bob, "Bob-graceful")) {
        return false;
      }
      exchange_uids();
      std::int64_t n = 0;
      std::int64_t mn = 0;
      std::int64_t p99 = 0;
      std::uint32_t g = 0;
      if (!wait_warmup(bob, false, &n, &mn, &p99, &g, nullptr, nullptr)) {
        return false;
      }
      std::int64_t alice_n2 = 0;
      std::int64_t alice_min2 = 0;
      std::int64_t alice_p992 = 0;
      std::int64_t alice_srv = 0;
      std::int64_t alice_proto = 0;
      if (!wait_warmup(alice, true, &alice_n2, &alice_min2, &alice_p992, nullptr,
                       &alice_srv, &alice_proto)) {
        return false;
      }
      if (alice_srv != 0) {
        dest = alice_srv;
      }
      return true;
    };
    if (!restart_for_graceful()) {
      ok_phase = false;
      std::cerr << "FAIL could not restart pair for graceful Unknown control\n";
      FailedCase fc{};
      fc.invariant = "reporting/harness failure: graceful restart failed";
      failed.push_back(fc);
    } else {
      wait_window_closed(8000);
      SendRaw(bob, kIpcAnnounceUnknown);
      auto const deadline = GetTickCount64() + 15000;
      while (GetTickCount64() < deadline) {
        auto q = query_ckpt(0, false);
        if (q && q->state == 2) {
          graceful_hit = 1;
          break;
        }
      }
    }
    if (graceful_hit != 1) {
      ok_phase = false;
      std::cerr << "FAIL graceful control did not yield Unknown/state 2\n";
      FailedCase fc{};
      fc.invariant = "observer saw the wrong state (graceful Unknown expected)";
      failed.push_back(fc);
    }
    }  // run_controls

    std::vector<double> sched_err_ms;
    std::vector<double> send_err_ms;
    std::vector<double> interval_err_ms;
    std::vector<double> req_margin;
    std::vector<double> resp_margin;
    int req_n = 0;
    int req_before = 0;
    int req_after = 0;
    int req_none = 0;
    int resp_n = 0;
    int resp_before = 0;
    int resp_after = 0;
    int resp_none = 0;
    std::vector<int> margin_lt10;
    std::vector<int> late_cases;
    for (auto const& c : cycles) {
      if (c.scheduled_nominal_us == 0 || c.expected_nominal_us == 0) {
        continue;
      }
      sched_err_ms.push_back(
          static_cast<double>(c.scheduled_nominal_us - c.expected_nominal_us) /
          1000.0);
      if (c.actual_first_send_us != 0) {
        send_err_ms.push_back(
            static_cast<double>(c.actual_first_send_us - c.expected_nominal_us) /
            1000.0);
      }
    }
    for (std::size_t i = 1; i < cycles.size(); ++i) {
      if (cycles[i].actual_first_send_us == 0 ||
          cycles[i - 1].actual_first_send_us == 0) {
        continue;
      }
      interval_err_ms.push_back(
          static_cast<double>(cycles[i].actual_first_send_us -
                              cycles[i - 1].actual_first_send_us - period_us) /
          1000.0);
    }
    for (int i = 0; i < static_cast<int>(cycles.size()); ++i) {
      auto const& c = cycles[static_cast<std::size_t>(i)];
      if (c.fault_mode == 1) {
        ++req_n;
        if (c.no_retry) {
          ++req_none;
        } else if (c.retry_before_deadline) {
          ++req_before;
          if (std::isfinite(c.retry_server_margin_ms)) {
            req_margin.push_back(c.retry_server_margin_ms);
            if (c.retry_server_margin_ms < 10.0) {
              margin_lt10.push_back(i);
            }
          }
        } else if (c.retry_after_deadline) {
          ++req_after;
          late_cases.push_back(i);
        }
      } else if (c.fault_mode == 2) {
        ++resp_n;
        if (c.no_retry) {
          ++resp_none;
        } else if (c.retry_before_deadline) {
          ++resp_before;
          if (std::isfinite(c.retry_server_margin_ms)) {
            resp_margin.push_back(c.retry_server_margin_ms);
            if (c.retry_server_margin_ms < 10.0) {
              margin_lt10.push_back(i);
            }
          }
        } else if (c.retry_after_deadline) {
          ++resp_after;
          late_cases.push_back(i);
        }
      }
    }
    auto slope_ms = [&](std::vector<double> const& y) {
      auto const n = static_cast<double>(y.size());
      if (n < 2) {
        return 0.0;
      }
      double sum_x = 0;
      double sum_y = 0;
      double sum_xx = 0;
      double sum_xy = 0;
      for (std::size_t i = 0; i < y.size(); ++i) {
        auto const x = static_cast<double>(i);
        sum_x += x;
        sum_y += y[i];
        sum_xx += x * x;
        sum_xy += x * y[i];
      }
      auto const den = n * sum_xx - sum_x * sum_x;
      if (den == 0) {
        return 0.0;
      }
      return (n * sum_xy - sum_x * sum_y) / den;
    };
    auto mean_of = [&](std::vector<double> const& y) {
      if (y.empty()) {
        return 0.0;
      }
      return std::accumulate(y.begin(), y.end(), 0.0) /
             static_cast<double>(y.size());
    };
    auto min_of = [&](std::vector<double> const& y) {
      if (y.empty()) {
        return 0.0;
      }
      return *std::min_element(y.begin(), y.end());
    };
    auto max_of = [&](std::vector<double> const& y) {
      if (y.empty()) {
        return 0.0;
      }
      return *std::max_element(y.begin(), y.end());
    };
    if (std::fabs(slope_ms(sched_err_ms)) > 0.01) {
      ok_phase = false;
      FailedCase fc{};
      fc.invariant = "next nominal schedule shifted (accumulating phase drift)";
      failed.push_back(fc);
      std::cerr << "FAIL accumulating scheduled phase drift slope="
                << F3(slope_ms(sched_err_ms)) << " ms/cycle\n";
    }

    std::ofstream failed_json(std::filesystem::path{args.artifact_dir} /
                              "failed-cases.json");
    failed_json << "[\n";
    for (std::size_t i = 0; i < failed.size(); ++i) {
      auto const& f = failed[i];
      if (i != 0) {
        failed_json << ",\n";
      }
      failed_json << "  {\"cycle\":" << f.c.index << ",\"seed\":" << args.seed
                  << ",\"transport\":\"" << args.transport
                  << "\",\"sequence\":\"" << f.c.sequence
                  << "\",\"fault_type\":\"" << f.c.fault_type
                  << "\",\"invariant\":\"" << f.invariant
                  << "\",\"logical_ping_id\":" << f.c.logical_ping_id
                  << ",\"phase_anchor_us\":" << f.c.phase_anchor_us
                  << ",\"expected_nominal_us\":" << f.c.expected_nominal_us
                  << ",\"original_nominal_us\":" << f.c.scheduled_nominal_us
                  << ",\"original_deadline_us\":" << f.c.original_deadline_us
                  << ",\"first_attempt_us\":" << f.c.actual_first_send_us
                  << ",\"first_request_sent\":" << json_i(f.c.first_request_sent)
                  << ",\"retry_decision_us\":" << json_i(f.c.retry_decision_us)
                  << ",\"retry_scheduled_us\":" << json_i(f.c.retry_scheduled_us)
                  << ",\"retry_send_us\":" << json_i(f.c.retry_send_us)
                  << ",\"retry_server_receive_mapped_us\":"
                  << json_i(f.c.retry_server_receive_mapped_us)
                  << ",\"retry_server_margin_ms\":"
                  << json_d(f.c.retry_server_margin_ms)
                  << ",\"original_window_start_us\":"
                  << f.c.original_window_start_us
                  << ",\"original_window_end_us\":" << f.c.original_window_end_us
                  << ",\"corrected_current_window_start_us\":"
                  << f.c.corrected_cur_start_us
                  << ",\"corrected_current_window_end_us\":"
                  << f.c.corrected_cur_end_us
                  << ",\"next_scheduled_nominal_us\":"
                  << f.c.next_scheduled_nominal_us
                  << ",\"observer_states\":[";
      for (std::size_t oi = 0; oi < f.c.observers.size(); ++oi) {
        if (oi != 0) {
          failed_json << ",";
        }
        auto const& h = f.c.observers[oi];
        failed_json << "{\"checkpoint\":" << h.checkpoint
                    << ",\"rel_deadline_us\":" << h.rel_deadline_us
                    << ",\"state\":" << h.state
                    << ",\"expected_state\":" << h.expected_state << "}";
      }
      failed_json << "]}";
    }
    failed_json << "\n]\n";

    std::ofstream report(std::filesystem::path{args.artifact_dir} / "report.md");
    if (!report) {
      std::cerr << "FAIL cannot write report.md\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }
    auto pct_block = [&](char const* title, std::vector<double> const& v) {
      report << "### " << title << "\n";
      report << "- n: " << v.size() << "\n";
      report << "- min: " << F3(min_of(v)) << "\n";
      report << "- mean: " << F3(mean_of(v)) << "\n";
      report << "- p50: " << F3(Percentile(v, 0.50)) << "\n";
      report << "- p90: " << F3(Percentile(v, 0.90)) << "\n";
      report << "- p95: " << F3(Percentile(v, 0.95)) << "\n";
      report << "- p99: " << F3(Percentile(v, 0.99)) << "\n";
      report << "- max: " << F3(max_of(v)) << "\n\n";
    };
    report << "# UAP 1s nominal phase preservation\n\n";
    report << "- transport: " << args.transport << "\n";
    report << "- seed: " << args.seed << "\n";
    report << "- mode: " << (stress ? "stress" : "fast") << "\n";
    report << "- steps: " << steps.size() << "\n";
    report << "- phase_anchor_us: " << phase_anchor_us << "\n";
    report << "- period_us: " << period_us << "\n";
    report << "- one_way_us (coordinator mapping, min_rtt/2): " << one_way_us
           << "\n";
    report << "- first ping is sent at Tn - attempt_lead (PingCloudServers::"
              "MakePing / ApplyLogicalPingAttempt first_attempt_at).\n";
    report << "- retry is scheduled immediately on ping error 2 "
              "(ScheduleSameCycleRetry); boundary cases hold send until "
              "Tn+offset-one_way via PingTestFaults hold.\n";
    report << "- Alice queries via Client::QueryPeerReceiveSchedule at "
              "checkpoints 1-5 (IPC QueryNow).\n";
    report << "- retry vs original deadline uses coordinator QPC mapping: "
              "retry_send_qpc + one_way vs cycle_anchor mapped from the same "
              "Bob trace event. Alice last_online is recorded separately and "
              "is not replaced with a raw client send timestamp.\n\n";
    report << "## Schedule phase\n\n";
    pct_block("scheduled phase error (ms)", sched_err_ms);
    pct_block("actual first-attempt send phase error (ms)", send_err_ms);
    pct_block("actual interval error from 1000 ms (ms)", interval_err_ms);
    report << "- final scheduled phase error ms: "
           << (sched_err_ms.empty() ? "0" : F3(sched_err_ms.back())) << "\n";
    double max_abs_sched = 0;
    for (auto x : sched_err_ms) {
      max_abs_sched = std::max(max_abs_sched, std::fabs(x));
    }
    report << "- maximum cumulative |scheduled phase error| ms: "
           << F3(max_abs_sched) << "\n";
    report << "- linear drift slope ms/cycle: " << F3(slope_ms(sched_err_ms))
           << "\n\n";
    auto retry_block = [&](char const* title, int n, int before, int after,
                           int none, std::vector<double> const& m) {
      report << "### " << title << "\n";
      report << "- attempted cases: " << n << "\n";
      report << "- retry received before original deadline: " << before << "\n";
      report << "- retry received after original deadline: " << after << "\n";
      report << "- no retry received: " << none << "\n";
      report << "- success rate: "
             << (n == 0 ? 0.0 : static_cast<double>(before) / n) << "\n";
      pct_block("retry_server_margin ms", m);
    };
    report << "## Retry arrival before original deadline\n\n";
    retry_block("request loss", req_n, req_before, req_after, req_none,
                req_margin);
    retry_block("response loss", resp_n, resp_before, resp_after, resp_none,
                resp_margin);
    report << "- cases with margin < 10 ms:";
    if (margin_lt10.empty()) {
      report << " none\n";
    } else {
      report << "\n";
      for (auto i : margin_lt10) {
        auto const& c = cycles[static_cast<std::size_t>(i)];
        report << "  - cycle " << i << " " << c.sequence << " " << c.fault_type
               << " margin_ms=" << csv_d(c.retry_server_margin_ms) << "\n";
      }
    }
    report << "- late cases:";
    if (late_cases.empty()) {
      report << " none\n\n";
    } else {
      report << "\n";
      for (auto i : late_cases) {
        auto const& c = cycles[static_cast<std::size_t>(i)];
        report << "  - cycle " << i << " " << c.sequence << " " << c.fault_type
               << " margin_ms=" << csv_d(c.retry_server_margin_ms)
               << " production_observer_state_after_Tn: QueryPeerReceiveSchedule "
                  "remains kExpected while nextPingDelta still points at Tn+1 "
                  "unless every expected server returns a negative delta.\n";
      }
      report << "\n";
    }
    report << "## Window correction\n\n";
    report << "See window-corrections.csv. Next nominal phase delta is "
              "next_scheduled_nominal - (expected + 1000ms).\n\n";
    report << "## Observer results\n\n";
    report << "Alice calls Client::QueryPeerPresence(peer_uid) through "
              "RoleState::QueryNow at:\n";
    report << "1. immediately after the first-attempt trace is captured for the "
              "armed ping (Bob has already advertised Tn; QueryNow is not "
              "inserted before Consume)\n";
    report << "2. after AttemptTimeout, before retry send (hold provides the "
              "race window on boundary cases)\n";
    report << "3. after retry RequestSent, before original deadline\n";
    report << "4. immediately after original deadline\n";
    report << "5. after CycleConfirmed / next nominal\n\n";
    report << "- query_failures: " << query_failures << "\n";
    report << "- false live MissedDeadline: " << live_false_md << "\n";
    report << "- false live Unknown: " << live_false_unknown << "\n\n";
    report << "## Reliability\n\n";
    report << "- request-loss recovery: " << req_before << "/" << req_n << "\n";
    report << "- response-loss recovery: " << resp_before << "/" << resp_n
           << "\n";
    report << "- duplicate logical ping count: " << duplicate_logical << "\n";
    report << "- in-flight query reused: " << inflight_reused << "\n";
    report << "- in-flight query skipped: " << inflight_skipped << "\n";
    report << "- extra in-flight subscribers: " << inflight_extra << "\n";
    report << "- graceful Unknown: " << graceful_hit << "/1\n";
    report << "- hard-stop MissedDeadline: " << hard_hit << "/1\n";
    report << "- failed invariants: " << failed.size() << "\n";
    report << "\nHard-stop timing is Test-harness MissedDeadline detection "
              "latency, not a production SLA.\n";
    report << "Semantic results: hard-stop MissedDeadline/state 1; "
              "graceful-stop Unknown/state 2.\n";
    report.flush();
    if (!report) {
      std::cerr << "FAIL report.md write failed\n";
      StopChild(alice);
      StopChild(bob);
      return 8;
    }
    jsonl.flush();
    samples_csv.flush();
    phase_csv.flush();
    win_csv.flush();
    obs_csv.flush();
    std::cout << "report="
              << (std::filesystem::path{args.artifact_dir} / "report.md")
              << std::endl;
    std::cout << (ok_phase ? "PASS phase-preservation" : "FAIL phase-preservation")
              << std::endl;
    StopChild(alice);
    StopChild(bob);
    return ok_phase ? 0 : 7;

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

#include "stream_matrix/bench_runner.h"

#include <chrono>
#include <memory>
#include <random>
#include <thread>
#include <utility>

#include "stream_matrix/bench_udp_budget.h"

#define AE_EXAMPLE_ETHERNET 1

#include "../../common/aether_construct_ethernet.h"

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_transport.h"

namespace ae::stream_matrix_bench {
Uid const& StreamMatrixBench::ParentUid() {
  static Uid const kUid =
      Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  return kUid;
}

namespace {

Duration MsToDuration(double ms) {
  return std::chrono::duration_cast<Duration>(
      std::chrono::duration<double, std::milli>(ms));
}

}  // namespace

TimePoint StreamMatrixBench::BenchTimeToAe(
    StreamMatrixBench::BenchClock::time_point tp) {
  return TimePoint{} + std::chrono::duration_cast<std::chrono::microseconds>(
                           tp.time_since_epoch());
}

StreamMatrixBench::BenchClock::time_point StreamMatrixBench::AeToBenchTime(
    TimePoint tp) {
  return StreamMatrixBench::BenchClock::time_point{
      std::chrono::duration_cast<StreamMatrixBench::BenchClock::duration>(
          tp.time_since_epoch())};
}

StreamMatrixBench::BenchClock::time_point StreamMatrixBench::BenchNow() const {
  return BenchClock::now();
}

BenchCliOptions ParseArgs(int argc, char* argv[]) {
  BenchCliOptions options;
#ifdef AETHER_STREAM_MATRIX_SDK_SHA
  options.sdk_sha = AETHER_STREAM_MATRIX_SDK_SHA;
#endif
#ifdef AETHER_STREAM_MATRIX_SDK_DIRTY
  options.sdk_tree_dirty = (AETHER_STREAM_MATRIX_SDK_DIRTY != 0);
#endif
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--smoke") {
      options.smoke = true;
    } else if (arg == "--full") {
      options.full_matrix = true;
    } else if (arg == "--max-rate" && i + 1 < argc) {
      options.max_messages_per_sec = std::stod(argv[++i]);
    } else if (arg == "--warmup-sec" && i + 1 < argc) {
      options.warmup = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--measure-sec" && i + 1 < argc) {
      options.measure = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--late-sec" && i + 1 < argc) {
      options.late_collect = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--output" && i + 1 < argc) {
      options.output_dir = argv[++i];
    } else if (arg == "--sdk-sha" && i + 1 < argc) {
      options.sdk_sha = argv[++i];
    } else if (arg == "--server-version" && i + 1 < argc) {
      options.server_version = argv[++i];
    }
  }
  if (!options.smoke && !options.full_matrix) {
    options.smoke = true;
  }
  return options;
}

StreamMatrixBench::StreamMatrixBench(BenchCliOptions options)
    : options_{std::move(options)} {
  safe_stream_config_ = SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 10,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{200},
  };
  safe_doc_.capacity = AE_SAFE_STREAM_CAPACITY;
  safe_doc_.config = safe_stream_config_;
}

bool StreamMatrixBench::PumpUntil(std::function<bool()> predicate,
                                  Duration timeout) {
  auto const deadline = Now() + timeout;
  while (!app_->IsExited()) {
    if (predicate()) {
      return true;
    }
    if (Now() >= deadline) {
      return false;
    }
    OnTick(BenchNow());
    auto next_time = app_->Update(Now());
    app_->WaitUntil(std::min(next_time, Now() + std::chrono::milliseconds{5}));
  }
  return false;
}

void StreamMatrixBench::PumpIdleUntil(TimePoint until) {
  while (!app_->IsExited() && Now() < until) {
    OnTick(BenchNow());
    auto next_time = app_->Update(Now());
    app_->WaitUntil(std::min(next_time, std::min(until, Now() + std::chrono::milliseconds{5})));
  }
}

SetupResult StreamMatrixBench::SetupMode(BenchStreamKind stream,
                                         BenchTransportKind transport) {
  TeardownStreams();
  write_status_subs_.Reset();
  recv_sub_.Reset();
  send_stream_ = nullptr;

  if (!ModeCompiledIn(transport)) {
    log_stream_ << "SetupMode: transport not compiled in\n";
    return SetupResult::kNotCompiled;
  }

  using namespace std::chrono_literals;
  client_a_->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(5s));
  client_b_->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(5s));

  auto handle_a =
      client_a_->message_stream_manager().CreatePort(client_b_->uid());
  auto handle_b =
      client_b_->message_stream_manager().CreatePort(client_a_->uid());

  p2p_a_ = std::make_shared<P2pStream>(*ae_context_, client_a_.Load(),
                                       client_b_->uid(), std::move(handle_a));
  p2p_b_ = std::make_shared<P2pStream>(*ae_context_, client_b_.Load(),
                                       client_a_->uid(), std::move(handle_b));

  if (!PumpUntil(
          [&]() {
            return p2p_a_->stream_info().link_state == LinkState::kLinked &&
                   p2p_b_->stream_info().link_state == LinkState::kLinked;
          },
          std::chrono::seconds{120})) {
    log_stream_ << "SetupMode: link timeout\n";
    return SetupResult::kSetupFailed;
  }

  auto verify_a = VerifyP2pStreamTransport(*p2p_a_, transport);
  auto verify_b = VerifyP2pStreamTransport(*p2p_b_, transport);
  if (verify_a != TransportVerifyResult::kOk ||
      verify_b != TransportVerifyResult::kOk) {
    log_stream_ << "SetupMode: transport verify failed a="
                << static_cast<int>(verify_a) << " b="
                << static_cast<int>(verify_b) << '\n';
    return SetupResult::kTransportUnavailable;
  }
  active_transport_ = transport;
  active_stream_ = stream;
  transport_verified_ = true;

  if (stream == BenchStreamKind::kP2pSafeStream) {
    safe_a_ = make_unique<P2pSafeStream>(*ae_context_, safe_stream_config_,
                                         p2p_a_);
    safe_b_ = make_unique<P2pSafeStream>(*ae_context_, safe_stream_config_,
                                         p2p_b_);
    send_stream_ = safe_a_.get();
    safe_doc_.max_element_size_reported = safe_a_->stream_info().max_element_size;
    safe_doc_.rec_element_size_reported = safe_a_->stream_info().rec_element_size;
    recv_sub_ = safe_b_->out_data_event().Subscribe(
        [this](DataBuffer const& data) { OnReceived(data); });
    recv_sub_a_ = safe_a_->out_data_event().Subscribe(
        [this](DataBuffer const& data) { OnReceived(data); });
  } else {
    send_stream_ = p2p_a_.get();
    recv_sub_ = p2p_b_->out_data_event().Subscribe(
        [this](DataBuffer const& data) { OnReceived(data); });
    recv_sub_a_ = p2p_a_->out_data_event().Subscribe(
        [this](DataBuffer const& data) { OnReceived(data); });
  }

  if (!PumpUntil(
          [&]() {
            return send_stream_->stream_info().link_state == LinkState::kLinked;
          },
          std::chrono::seconds{30})) {
    log_stream_ << "SetupMode: send stream link timeout\n";
    return SetupResult::kSetupFailed;
  }

  if (!RunServiceExchange()) {
    log_stream_ << "SetupMode: service exchange failed\n";
    return SetupResult::kSetupFailed;
  }

  return SetupResult::kOk;
}

void StreamMatrixBench::SendServiceEnvelope(std::uint64_t attempt_id,
                                            std::uint64_t point_id,
                                            ByteIStream& stream) {
  auto service_payload = MakePayloadPattern(16);
  BenchEnvelope envelope{
      .kind = static_cast<std::uint8_t>(EnvelopeKind::kService),
      .run_id = run_id_,
      .point_id = point_id,
      .sequence = attempt_id,
      .payload = std::move(service_payload),
  };
  auto wire = SerializeEnvelope(envelope);
  auto& wa = stream.Write(std::move(wire));
  write_status_subs_ += wa.status_event().Subscribe(
      [](WriteAction::Status) {});
}

bool StreamMatrixBench::RunServiceExchange() {
  service_rx_.store(false);
  service_echo_pending_.store(false);
  expected_service_attempt_id_ = ++service_attempt_id_;
  in_service_exchange_ = true;

  SendServiceEnvelope(expected_service_attempt_id_, 0, *send_stream_);
  if (!PumpUntil([&]() { return service_echo_pending_.load(); },
                 std::chrono::seconds{45})) {
    in_service_exchange_ = false;
    write_status_subs_.Reset();
    return false;
  }
  ByteIStream* b_send = p2p_b_.get();
  if (safe_b_) {
    b_send = safe_b_.get();
  }
  SendServiceEnvelope(expected_service_attempt_id_, 1, *b_send);
  if (!PumpUntil([&]() { return service_rx_.load(); },
                 std::chrono::seconds{45})) {
    in_service_exchange_ = false;
    write_status_subs_.Reset();
    return false;
  }
  in_service_exchange_ = false;
  write_status_subs_.Reset();
  return true;
}

void StreamMatrixBench::TeardownStreams() {
  point_active_ = false;
  accept_measure_ = false;
  write_status_subs_.Reset();
  recv_sub_.Reset();
  recv_sub_a_.Reset();
  safe_a_.reset();
  safe_b_.reset();
  p2p_a_.reset();
  p2p_b_.reset();
  send_stream_ = nullptr;
}

void StreamMatrixBench::OnReceived(DataBuffer const& data) {
  auto envelope = DeserializeEnvelope(data);
  if (!envelope) {
    return;
  }
  if (envelope->run_id != run_id_) {
    return;
  }
  if (IsServiceEnvelope(*envelope)) {
    if (envelope->sequence != expected_service_attempt_id_) {
      return;
    }
    if (!in_service_exchange_) {
      return;
    }
    if (envelope->point_id == 0) {
      service_echo_pending_.store(true);
    } else if (envelope->point_id == 1) {
      service_rx_.store(true);
    }
    return;
  }
  if (IsWarmupEnvelope(*envelope)) {
    return;
  }
  if (!accept_measure_ || !IsMeasureEnvelope(*envelope)) {
    return;
  }
  if (envelope->point_id != current_point_id_) {
    return;
  }
  point_stats_.NoteReceivedMeasure(envelope->sequence, BenchTimeToAe(BenchNow()),
                                   envelope->payload.size());
}

void StreamMatrixBench::OnTick(StreamMatrixBench::BenchClock::time_point now) {
  if (!point_active_ || !send_stream_) {
    return;
  }
  if (now >= collect_end_) {
    point_active_ = false;
    accept_measure_ = false;
    return;
  }
  if (now < measure_begin_) {
    OnWarmupTick(now);
    return;
  }
  if (now >= measure_end_) {
    return;
  }

  while (next_scheduled_send_ < now && next_scheduled_send_ < measure_end_) {
    point_stats_.NoteMissedSchedule();
    next_scheduled_send_ +=
        std::chrono::duration_cast<BenchClock::duration>(MsToDuration(period_ms_));
  }

  while (now >= next_scheduled_send_ && now < measure_end_) {
    point_stats_.NoteScheduledMeasure();
    auto const sequence = next_sequence_++;
    auto scheduled = next_scheduled_send_;

    BenchEnvelope envelope{
        .kind = static_cast<std::uint8_t>(EnvelopeKind::kMeasure),
        .run_id = run_id_,
        .point_id = current_point_id_,
        .sequence = sequence,
        .payload = payload_pattern_,
    };
    auto wire = SerializeEnvelope(envelope);
    if (active_transport_ == BenchTransportKind::kUdp &&
        !BenchEnvelopeWithinUdpSocketBudget(wire.size(),
                                            p2p_a_->stream_info())) {
      log_stream_ << "point " << current_point_id_
                  << " UDP budget exceeded envelope_bytes=" << wire.size()
                  << '\n';
    }

    point_stats_.NoteWriteCalledMeasure(sequence, BenchTimeToAe(now),
                                        BenchTimeToAe(scheduled));
    auto& wa = send_stream_->Write(std::move(wire));
    write_status_subs_ += wa.status_event().Subscribe(
        [this](WriteAction::Status status) {
          if (status == WriteAction::Status::kSuccess) {
            point_stats_.NoteWriteOkMeasure();
          } else if (status == WriteAction::Status::kFail) {
            point_stats_.NoteWriteFailedMeasure();
          } else if (status == WriteAction::Status::kStop) {
            point_stats_.NoteWriteStoppedMeasure();
          }
        });

    next_scheduled_send_ +=
        std::chrono::duration_cast<BenchClock::duration>(MsToDuration(period_ms_));
  }
}

void StreamMatrixBench::OnWarmupTick(
    StreamMatrixBench::BenchClock::time_point now) {
  if (!point_active_ || !send_stream_) {
    return;
  }
  if (now < measure_begin_) {
    if (now >= next_warmup_send_) {
      BenchEnvelope envelope{
          .kind = static_cast<std::uint8_t>(EnvelopeKind::kWarmup),
          .run_id = run_id_,
          .point_id = current_point_id_,
          .sequence = next_sequence_++,
          .payload = payload_pattern_,
      };
      auto& wa = send_stream_->Write(SerializeEnvelope(envelope));
      write_status_subs_ += wa.status_event().Subscribe(
          [](WriteAction::Status) {});
      next_warmup_send_ +=
          std::chrono::duration_cast<BenchClock::duration>(MsToDuration(period_ms_));
    }
  }
}

CsvRow StreamMatrixBench::MakeOutcomeRow(BenchStreamKind stream,
                                         BenchTransportKind transport,
                                         std::string outcome, double period_ms) {
  CsvRow row;
  row.run_id = std::to_string(run_id_);
  row.point_id = "0";
  row.transport = std::string{TransportKindName(transport)};
  row.stream = std::string{StreamKindName(stream)};
  row.period_ms = period_ms;
  row.max_udp_payload_bytes = kBenchmarkUdpPayloadBudgetBytes;
  row.sdk_sha = options_.sdk_sha;
  if (options_.sdk_tree_dirty) {
    row.sdk_sha += "-dirty";
  }
  row.server_version = options_.server_version;
  row.outcome = std::move(outcome);
  return row;
}

std::optional<CsvRow> StreamMatrixBench::RunPoint(
    BenchStreamKind stream, BenchTransportKind transport,
    std::size_t payload_bytes, double period_ms,
    std::string const& outcome_if_skip) {
  if (!send_stream_) {
    return MakeOutcomeRow(stream, transport, "SETUP_FAILED", period_ms);
  }
  if (!outcome_if_skip.empty()) {
    auto row = MakeOutcomeRow(stream, transport, outcome_if_skip, period_ms);
    row.payload_bytes = payload_bytes;
    return row;
  }

  payload_bytes_ = payload_bytes;
  period_ms_ = period_ms;
  payload_pattern_ = MakePayloadPattern(payload_bytes);
  serialized_payload_ = SerializeEnvelope(
      BenchEnvelope{.kind = static_cast<std::uint8_t>(EnvelopeKind::kMeasure),
                    .run_id = run_id_,
                    .point_id = current_point_id_,
                    .sequence = next_sequence_,
                    .payload = payload_pattern_});

  write_status_subs_.Reset();
  current_point_id_ = next_point_id_++;

  point_begin_ = BenchNow();
  measure_begin_ = point_begin_ + options_.warmup;
  measure_end_ = measure_begin_ + options_.measure;
  collect_end_ = measure_end_ + options_.late_collect;

  point_stats_.BeginPoint(BenchTimeToAe(measure_begin_),
                          BenchTimeToAe(measure_end_),
                          BenchTimeToAe(collect_end_));
  next_scheduled_send_ = measure_begin_;
  next_warmup_send_ =
      point_begin_ + std::chrono::duration_cast<BenchClock::duration>(
                         MsToDuration(period_ms_));
  point_active_ = true;
  accept_measure_ = false;
  write_status_subs_.Reset();

  while (!app_->IsExited() && BenchNow() < measure_begin_) {
    OnTick(BenchNow());
    app_->Update(Now());
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  accept_measure_ = true;

  while (!app_->IsExited() && BenchNow() < collect_end_) {
    OnTick(BenchNow());
    app_->Update(Now());
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  point_active_ = false;
  accept_measure_ = false;
  write_status_subs_.Reset();
  point_stats_.FinalizeAfterCollectEnd();

  CsvRow row;
  row.run_id = std::to_string(run_id_);
  row.point_id = std::to_string(current_point_id_);
  row.transport = std::string{TransportKindName(transport)};
  row.stream = std::string{StreamKindName(stream)};
  row.payload_bytes = payload_bytes;
  row.serialized_bytes = serialized_payload_.size();
  row.max_udp_payload_bytes = kBenchmarkUdpPayloadBudgetBytes;
  row.period_ms = period_ms;
  row.counters = point_stats_.counters();
  row.latency = point_stats_.SendReceiveLatency();
  row.schedule_latency = point_stats_.ScheduleLatency();
  row.schedule_receive_latency = point_stats_.ScheduleReceiveLatency();
  row.send_rate = point_stats_.SendRatePerSec();
  row.receive_rate = point_stats_.ReceiveRatePerSec();
  row.goodput_bytes_s = point_stats_.GoodputBytesPerSec();
  row.sdk_sha = options_.sdk_sha;
  if (options_.sdk_tree_dirty) {
    row.sdk_sha += "-dirty";
  }
  row.server_version = options_.server_version;

  if (row.counters.write_calls_measure > 0) {
    row.delivery_loss_pct =
        100.0 *
        static_cast<double>(row.counters.missing_after_deadline) /
        static_cast<double>(row.counters.write_calls_measure);
  }

  if (active_transport_ == BenchTransportKind::kUdp &&
      !BenchEnvelopeWithinUdpSocketBudget(serialized_payload_.size(),
                                          p2p_a_->stream_info())) {
    row.outcome = "UDP_BUDGET_EXCEEDED";
  } else if (row.counters.rx_unique_measure == 0 &&
             row.counters.write_ok_measure > 0) {
    row.outcome = "RX_MISSING";
  } else if (row.counters.write_calls_measure == 0 &&
             row.counters.skipped_backpressure > 0) {
    row.outcome = "WRITE_BACKPRESSURE";
  } else if (row.counters.write_failed_measure > 0 ||
             row.counters.write_stopped_measure > 0 ||
             row.counters.write_immediate_fail_measure > 0) {
    row.outcome = "WRITE_ERRORS";
  } else if (row.counters.rx_unique_by_deadline > 0) {
    row.outcome = "OK";
  } else {
    row.outcome = "NO_MEASURE_TRAFFIC";
  }
  return row;
}

bool StreamMatrixBench::EvaluateSmokeRow(CsvRow const& row) const {
  if (!options_.smoke) {
    return true;
  }
  if (row.payload_bytes != 100) {
    return false;
  }
  if (row.period_ms != 1000.0) {
    return false;
  }
  if (row.outcome != "OK") {
    return false;
  }
  if (!transport_verified_) {
    return false;
  }
  auto const expect_sends = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(options_.measure)
          .count());
  if (row.counters.scheduled_measure != expect_sends) {
    return false;
  }
  if (row.counters.write_calls_measure != expect_sends) {
    return false;
  }
  if (row.counters.write_ok_measure != expect_sends) {
    return false;
  }
  if (row.counters.rx_unique_by_deadline != expect_sends) {
    return false;
  }
  if (row.counters.missing_after_deadline != 0) {
    return false;
  }
  if (row.counters.write_failed_measure != 0 ||
      row.counters.write_stopped_measure != 0 ||
      row.counters.write_immediate_fail_measure != 0) {
    return false;
  }
  return true;
}

int StreamMatrixBench::Run() {
  std::filesystem::create_directories(options_.output_dir);
  csv_stream_.open(options_.output_dir / "results.csv",
                   std::ios::out | std::ios::trunc);
  log_stream_.open(options_.output_dir / "run.log",
                   std::ios::out | std::ios::trunc);
  WriteCsvHeader(csv_stream_);

  log_stream_ << "sdk_udp=" << kSdkUdpEnabled << " sdk_tcp=" << kSdkTcpEnabled
              << '\n';

  app_ = examples::construct_aether_app();
  ae_context_.emplace(*app_);

  app_->aether()->SelectClient(ParentUid(), "sender").result_event().Subscribe(
      [&](auto const& res) {
        if (res) {
          client_a_ = res.value();
        }
      });
  app_->aether()->SelectClient(ParentUid(), "receiver").result_event().Subscribe(
      [&](auto const& res) {
        if (res) {
          client_b_ = res.value();
        }
      });

  if (!PumpUntil([&]() { return client_a_ && client_b_; },
                 std::chrono::seconds{180})) {
    log_stream_ << "Failed to acquire clients\n";
    return 1;
  }

  {
    auto settle_until = Now() + std::chrono::seconds{6};
    PumpIdleUntil(settle_until);
    client_a_->cloud_connection().Restream();
    client_b_->cloud_connection().Restream();
  }

  struct Mode {
    BenchStreamKind stream;
    BenchTransportKind transport;
  };
  run_id_ = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()) |
            1ULL;
  log_stream_ << "run_id=" << run_id_ << '\n';

  std::vector<Mode> modes;
  if (kSdkUdpEnabled) {
    modes.push_back({BenchStreamKind::kP2pStream, BenchTransportKind::kUdp});
    modes.push_back(
        {BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp});
  }
  if (kSdkTcpEnabled) {
    modes.push_back({BenchStreamKind::kP2pStream, BenchTransportKind::kTcp});
    modes.push_back(
        {BenchStreamKind::kP2pSafeStream, BenchTransportKind::kTcp});
  }
  if (options_.smoke) {
    smoke_modes_expected_ = static_cast<int>(modes.size());
  }

  for (auto const& mode : modes) {
    auto setup = SetupMode(mode.stream, mode.transport);
    if (setup != SetupResult::kOk) {
      std::string outcome = "SETUP_FAILED";
      if (setup == SetupResult::kNotCompiled) {
        outcome = "TRANSPORT_UNAVAILABLE";
      } else if (setup == SetupResult::kTransportUnavailable) {
        outcome = "TRANSPORT_UNAVAILABLE";
      }
      WriteCsvRow(csv_stream_, MakeOutcomeRow(mode.stream, mode.transport, outcome));
      csv_stream_.flush();
      TeardownStreams();
      continue;
    }
    ++modes_setup_ok_;
    transport_verified_ = true;

    PayloadLimitResult limit = ComputePayloadLimit(
        mode.stream, mode.transport, p2p_a_->stream_info(), safe_doc_,
        MeasureEnvelopeOverheadBytes());

    std::vector<double> periods =
        options_.smoke ? PeriodsMsFirstSecond()
                       : PeriodsMsFirstSecond();
    if (options_.full_matrix) {
      auto fast = PeriodsMsFast(options_.max_messages_per_sec);
      periods.insert(periods.end(), fast.begin(), fast.end());
    }

    for (double period : periods) {
      auto sizes = options_.smoke
                       ? std::vector<std::size_t>{100}
                       : BuildSizeMatrix(limit.max_payload_bytes, true);

      for (std::size_t size : sizes) {
        std::string skip;
        if (size > limit.max_payload_bytes) {
          skip = "SKIP_SIZE_LIMIT";
        }
        auto row = RunPoint(mode.stream, mode.transport, size, period, skip);
        if (row) {
          WriteCsvRow(csv_stream_, *row);
          csv_stream_.flush();
          if (options_.smoke && skip.empty() && EvaluateSmokeRow(*row)) {
            ++smoke_modes_passed_;
          }
        }
      }
    }
    TeardownStreams();
    transport_verified_ = false;
  }

  log_stream_ << "modes_setup_ok=" << modes_setup_ok_
              << " smoke_expected=" << smoke_modes_expected_
              << " smoke_passed=" << smoke_modes_passed_ << '\n';
  app_->Exit(0);

  if (options_.smoke) {
    if (smoke_modes_passed_ == smoke_modes_expected_ &&
        smoke_modes_expected_ > 0) {
      return 0;
    }
    if (smoke_modes_passed_ > 0) {
      return 2;
    }
    return modes_setup_ok_ > 0 ? 2 : 3;
  }
  if (modes_setup_ok_ == 0) {
    return 3;
  }
  return 0;
}

}  // namespace ae::stream_matrix_bench

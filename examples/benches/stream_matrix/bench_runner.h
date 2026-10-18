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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_RUNNER_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_RUNNER_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aether/all.h"

#include "stream_matrix/bench_client_acquire.h"
#include "stream_matrix/bench_clock.h"
#include "stream_matrix/bench_limits.h"
#include "stream_matrix/bench_stats.h"
#include "stream_matrix/bench_streaming_receiver.h"

namespace ae::stream_matrix_bench {

enum class LoadMode { kBurst, kBackpressure };

struct BenchCliOptions {
  bool smoke{false};
  bool full_matrix{false};
  bool run_points{false};
  bool mi1_tcp{false};
  bool streaming_tcp_smoke{false};
  bool streaming_tcp_matrix{false};
  bool streaming_udp_smoke{false};
  bool streaming_udp_matrix{false};
  bool streaming_best_repeats{false};
  bool streaming_tcp_resume{false};
  std::optional<std::size_t> streaming_repro_chunk;
  std::optional<std::size_t> streaming_repro_outstanding;
  std::optional<LoadMode> streaming_repro_mode;
  std::size_t streaming_repro_prefix_points{6};
  std::optional<BenchStreamKind> only_stream;
  std::optional<BenchTransportKind> only_transport;
  std::vector<std::size_t> only_payloads;
  std::optional<double> only_period_ms;
  double max_messages_per_sec{128.0};
  Duration warmup{std::chrono::seconds{5}};
  Duration measure{std::chrono::seconds{60}};
  Duration late_collect{std::chrono::seconds{60}};
  std::filesystem::path output_dir{"stream_matrix_out"};
  std::string sdk_sha{"UNKNOWN"};
  std::string server_version{"UNKNOWN"};
  bool sdk_tree_dirty{false};
};

enum class SetupResult {
  kOk,
  kNotCompiled,
  kTransportUnavailable,
  kSetupFailed,
};

struct StreamingPlannedPoint {
  BenchTransportKind transport;
  BenchStreamKind stream;
  LoadMode mode;
  std::size_t chunk;
  std::size_t outstanding;
  int repeats;
};

class StreamMatrixBench {
 public:
  explicit StreamMatrixBench(BenchCliOptions options);

  int Run();
  void BenchPump(BenchDuration max_wait);
  struct EventPoolUsage {
    std::size_t registered_events{};
    std::size_t handler_relationships{};
    std::size_t events_capacity{};
    std::size_t handlers_capacity{};
  };
  EventPoolUsage EventPoolUsageNow() const;
  bool GeneratorWriteBudgetOk(std::size_t extra_in_flight_subs) const;
  void ScheduleAfterPump(std::function<void()> fn);
  MultiSubscription& WriteStatusSubs() { return write_status_subs_; }
  MultiSubscription& StreamingPointWriteSubs() {
    return streaming_point_write_subs_;
  }
  ByteIStream* SendStream() { return send_stream_; }

 private:
  int RunMi1Tcp();
  int RunStreamingTcp();
  int RunStreamingUdpSafe();
  int RunStreamingBestRepeats();
  int ExecuteStreamingPlan(std::vector<StreamingPlannedPoint> plan,
                           std::vector<std::uint8_t> const& blob,
                           std::filesystem::path const& csv_path,
                           bool append_csv, bool skip_completed);
  Duration SdkWaitCap(BenchDuration bench_remaining) const;
  void PumpSdkOnce(BenchInstant bench_now, BenchDuration bench_remaining);
  void DrainAfterPumpQueue();
  bool PumpUntil(std::function<bool()> predicate, Duration timeout);
  void PumpBenchUntil(BenchInstant bench_until,
                      std::function<bool()> stop_early = {});

  SetupResult SetupMode(BenchStreamKind stream, BenchTransportKind transport);
  bool RunServiceExchange();
  struct ServiceExchangeLog {
    bool ok{false};
    std::string failure_stage;
  };
  ServiceExchangeLog RunServiceExchangeLogged();
  void RouteInboundForStreaming(
      DataBuffer const& data,
      std::shared_ptr<StreamingReceiverState> const& receiver);
  void SetStreamingMeasureReceiver(
      std::shared_ptr<StreamingReceiverState> const& receiver);
  void TeardownStreams();

  std::optional<CsvRow> RunPoint(BenchStreamKind stream,
                                 BenchTransportKind transport,
                                 std::size_t payload_bytes, double period_ms,
                                 std::string const& outcome_if_skip);

  void OnTick(BenchInstant now);
  void OnWarmupTick(BenchInstant now);
  void OnReceived(DataBuffer const& data);
  bool EvaluateSmokeRow(CsvRow const& row) const;
  void SendServiceEnvelope(std::uint64_t attempt_id, std::uint64_t point_id,
                            ByteIStream& stream);

  static Uid const& ParentUid();
  CsvRow MakeOutcomeRow(BenchStreamKind stream, BenchTransportKind transport,
                        std::string outcome, double period_ms = 0.0);

  BenchCliOptions options_;
  std::unique_ptr<AetherApp> app_;
  std::optional<AeContext> ae_context_;
  Client::ptr client_a_;
  Client::ptr client_b_;
  ClientSelectOutcome select_a_{};
  ClientSelectOutcome select_b_{};

  SafeStreamConfig safe_stream_config_{};
  SafeStreamLimitsDoc safe_doc_{};

  std::shared_ptr<P2pStream> p2p_a_;
  std::shared_ptr<P2pStream> p2p_b_;
  std::unique_ptr<P2pSafeStream> safe_a_;
  std::unique_ptr<P2pSafeStream> safe_b_;
  ByteIStream* send_stream_{nullptr};
  Subscription recv_sub_;
  Subscription recv_sub_a_;
  Subscription measure_recv_sub_;
  MultiSubscription streaming_point_write_subs_;
  std::deque<std::function<void()>> after_pump_queue_;

  std::uint64_t run_id_{};
  std::uint64_t next_point_id_{1};
  std::uint64_t next_sequence_{1};
  std::uint64_t current_point_id_{0};
  std::uint64_t service_attempt_id_{0};
  std::uint64_t expected_service_attempt_id_{0};
  bool in_service_exchange_{false};
  std::atomic<bool> service_echo_pending_{false};

  PointStats point_stats_;
  MultiSubscription write_status_subs_;

  bool point_active_{false};
  bool accept_measure_{false};
  BenchInstant measure_begin_{};
  BenchInstant measure_end_{};
  BenchInstant collect_end_{};
  BenchInstant next_scheduled_send_{};
  BenchInstant next_warmup_send_{};
  BenchInstant point_begin_{};

  std::size_t payload_bytes_{};
  double period_ms_{1000.0};
  DataBuffer serialized_payload_;
  std::vector<std::uint8_t> payload_pattern_;

  std::atomic<bool> service_rx_{false};
  int modes_setup_ok_{0};
  int smoke_modes_expected_{0};
  int smoke_modes_passed_{0};
  bool transport_verified_{false};
  BenchTransportKind active_transport_{BenchTransportKind::kUdp};
  BenchStreamKind active_stream_{BenchStreamKind::kP2pStream};

  std::ofstream csv_stream_;
  std::ofstream log_stream_;
};

BenchCliOptions ParseArgs(int argc, char* argv[]);

}  // namespace ae::stream_matrix_bench

#endif

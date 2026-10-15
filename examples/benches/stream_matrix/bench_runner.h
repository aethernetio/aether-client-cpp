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
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aether/all.h"

#include "stream_matrix/bench_limits.h"
#include "stream_matrix/bench_stats.h"

namespace ae::stream_matrix_bench {

struct BenchCliOptions {
  bool smoke{false};
  bool full_matrix{false};
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

class StreamMatrixBench {
 public:
  explicit StreamMatrixBench(BenchCliOptions options);

  int Run();

 private:
  bool PumpUntil(std::function<bool()> predicate, Duration timeout);
  void PumpIdleUntil(TimePoint until);

  SetupResult SetupMode(BenchStreamKind stream, BenchTransportKind transport);
  bool RunServiceExchange();
  void TeardownStreams();

  std::optional<CsvRow> RunPoint(BenchStreamKind stream,
                                 BenchTransportKind transport,
                                 std::size_t payload_bytes, double period_ms,
                                 std::string const& outcome_if_skip);

  void OnTick(TimePoint now);
  void OnReceived(DataBuffer const& data);

  static Uid const& ParentUid();
  CsvRow MakeOutcomeRow(BenchStreamKind stream, BenchTransportKind transport,
                        std::string outcome, double period_ms = 0.0);

  BenchCliOptions options_;
  std::unique_ptr<AetherApp> app_;
  std::optional<AeContext> ae_context_;
  Client::ptr client_a_;
  Client::ptr client_b_;

  SafeStreamConfig safe_stream_config_{};
  SafeStreamLimitsDoc safe_doc_{};

  std::shared_ptr<P2pStream> p2p_a_;
  std::shared_ptr<P2pStream> p2p_b_;
  std::unique_ptr<P2pSafeStream> safe_a_;
  std::unique_ptr<P2pSafeStream> safe_b_;
  ByteIStream* send_stream_{nullptr};
  Subscription recv_sub_;

  std::uint64_t run_id_{1};
  std::uint64_t next_point_id_{1};
  std::uint64_t next_sequence_{1};
  std::uint64_t current_point_id_{0};

  PointStats point_stats_;
  MultiSubscription write_status_subs_;

  bool point_active_{false};
  bool accept_measure_{false};
  TimePoint measure_begin_{};
  TimePoint measure_end_{};
  TimePoint collect_end_{};
  TimePoint next_scheduled_send_{};
  std::size_t payload_bytes_{};
  double period_ms_{1000.0};
  DataBuffer serialized_payload_;
  std::vector<std::uint8_t> payload_pattern_;

  std::atomic<bool> service_rx_{false};
  bool any_point_ok_{false};
  int modes_setup_ok_{0};

  std::ofstream csv_stream_;
  std::ofstream log_stream_;
};

BenchCliOptions ParseArgs(int argc, char* argv[]);

}  // namespace ae::stream_matrix_bench

#endif

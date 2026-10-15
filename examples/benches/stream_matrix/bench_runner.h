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
  Duration measure{std::chrono::seconds{30}};
  Duration late_wait{std::chrono::seconds{10}};
  std::filesystem::path output_dir{"stream_matrix_out"};
  std::string sdk_sha{"UNKNOWN"};
  std::string server_version{"UNKNOWN"};
};

struct TransportSelection {
  std::optional<std::size_t> udp_priority;
  std::optional<std::size_t> tcp_priority;
};

class StreamMatrixBench {
 public:
  explicit StreamMatrixBench(BenchCliOptions options);

  int Run();

 private:
  bool PumpUntil(std::function<bool()> predicate, Duration timeout);
  TransportSelection DiscoverTransports();
  bool SetupMode(BenchStreamKind stream, BenchTransportKind transport,
                 TransportSelection const& selection);
  void TeardownStreams();
  std::optional<CsvRow> RunPoint(BenchStreamKind stream,
                                 BenchTransportKind transport,
                                 std::size_t payload_bytes, double period_ms);

  void OnTick(TimePoint now);
  void OnReceived(DataBuffer const& data);

  BenchCliOptions options_;
  std::unique_ptr<AetherApp> app_;
  std::optional<AeContext> ae_context_;
  Client::ptr client_a_;
  Client::ptr client_b_;

  static Uid const& ParentUid();

  SafeStreamConfig safe_stream_config_{};
  SafeStreamLimitsDoc safe_doc_{};

  std::shared_ptr<P2pStream> p2p_a_;
  std::shared_ptr<P2pStream> p2p_b_;
  std::unique_ptr<P2pSafeStream> safe_a_;
  std::unique_ptr<P2pSafeStream> safe_b_;
  ByteIStream* active_stream_{nullptr};
  Subscription port_sub_;
  Subscription recv_sub_;

  std::uint64_t next_sequence_{1};
  PointStats point_stats_;
  MultiSubscription write_status_subs_;

  bool point_active_{false};
  bool in_measure_{false};
  TimePoint point_begin_{};
  TimePoint measure_begin_{};
  TimePoint measure_end_{};
  TimePoint deadline_{};
  TimePoint next_scheduled_send_{};
  std::size_t payload_bytes_{};
  double period_ms_{1000.0};
  DataBuffer serialized_payload_;
  std::vector<std::uint8_t> payload_pattern_;

  std::ofstream csv_stream_;
  std::ofstream log_stream_;
};

BenchCliOptions ParseArgs(int argc, char* argv[]);

}  // namespace ae::stream_matrix_bench

#endif  // EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_RUNNER_H_

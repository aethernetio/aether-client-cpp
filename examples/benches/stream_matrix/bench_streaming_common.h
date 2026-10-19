/*
 * Copyright 2026 Aethernet Inc.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_COMMON_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_COMMON_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "stream_matrix/bench_throughput_frame.h"
#include "stream_matrix/bench_transport.h"

namespace ae::stream_matrix_bench {

struct StreamingRunResult {
  BenchTransportKind transport{};
  LoadMode mode{};
  BenchStreamKind stream{};
  std::size_t chunk_bytes{};
  std::size_t outstanding_limit{};
  int repeat{};
  std::uint64_t chunks_total{};
  std::size_t payload_total_bytes{kThroughputTotalBytes};
  std::uint64_t write_attempts{};
  std::uint64_t write_ok{};
  std::uint64_t write_fail{};
  std::uint64_t write_stop{};
  std::uint64_t write_pending{};
  std::uint64_t write_unknown{};
  std::uint64_t generator_limit_events{};
  std::uint64_t sdk_capacity_events{};
  std::uint64_t events_registered_start{};
  std::uint64_t events_registered_peak{};
  std::uint64_t events_registered_end{};
  std::uint64_t handlers_active_start{};
  std::uint64_t handlers_active_peak{};
  std::uint64_t handlers_active_end{};
  bool write_status_trusted{true};
  std::uint64_t peak_outstanding_writes{};
  std::uint64_t peak_outstanding_bytes{};
  std::uint64_t dup_chunks{};
  std::size_t rx_unique_bytes{};
  bool delivery_ok{false};
  bool budget_ok{true};
  bool integrity_ok{false};
  bool timed_out{false};
  double submit_duration_s{};
  double first_rx_s{};
  double last_rx_s{};
  double all_write_terminal_s{};
  double goodput_mib_s{};
  double write_terminal_p50_ms{};
  double write_terminal_p95_ms{};
  double write_to_rx_p50_ms{};
  double write_to_rx_p95_ms{};
  double write_to_rx_max_ms{};
  std::size_t udp_max_tx{};
  std::size_t udp_max_rx{};
  std::uint64_t udp_tx_violations{};
  std::uint64_t udp_rx_violations{};
  std::string outcome;
};

}  // namespace ae::stream_matrix_bench

#endif

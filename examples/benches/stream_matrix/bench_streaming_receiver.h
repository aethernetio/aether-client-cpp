/*
 * Copyright 2026 Aethernet Inc.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_RECEIVER_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_RECEIVER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aether/types/data_buffer.h"

#include "stream_matrix/bench_clock.h"
#include "stream_matrix/bench_throughput_frame.h"

namespace ae::stream_matrix_bench {

bool IsThptWireData(DataBuffer const& data);

class StreamingReceiverState {
 public:
  StreamingReceiverState(std::vector<std::uint8_t> const& blob,
                         std::uint64_t run_id);

  void Feed(DataBuffer const& data, BenchInstant now);
  void Reset(std::uint64_t run_id);

  bool Complete() const;
  std::size_t bytes_unique() const;
  std::uint64_t dup_chunks() const;
  bool integrity_fail() const;
  BenchInstant first_rx() const;
  BenchInstant last_rx() const;
  BenchInstant recv_time_for_chunk(std::uint64_t chunk_index) const;
  std::size_t received_chunk_count() const;

 private:
  void AcceptFrame(DataBuffer const& data, BenchInstant now);

  std::vector<std::uint8_t> const& blob_;
  std::uint64_t run_id_;
  std::vector<bool> received_;
  std::unordered_map<std::uint64_t, BenchInstant> chunk_recv_;
  std::unordered_set<std::uint64_t> seen_chunks_;
  std::size_t bytes_unique_{0};
  std::uint64_t dup_chunks_{0};
  bool integrity_fail_{false};
  BenchInstant first_rx_{};
  BenchInstant last_rx_{};
};

}  // namespace ae::stream_matrix_bench

#endif

/*
 * Copyright 2026 Aethernet Inc.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_LOAD_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_LOAD_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "stream_matrix/bench_clock.h"
#include "stream_matrix/bench_runner.h"
#include "stream_matrix/bench_streaming_common.h"
#include "stream_matrix/bench_streaming_receiver.h"

namespace ae::stream_matrix_bench {

struct PointRunArgs {
  LoadMode mode{};
  BenchStreamKind stream_kind{};
  std::size_t chunk_bytes{};
  std::size_t outstanding_limit{};
  std::uint64_t chunks_total{};
  std::uint64_t run_id{};
  std::vector<std::uint8_t> const* blob{};
  bool progress_log{false};
  std::filesystem::path progress_log_path;
  std::filesystem::path chunk_timeline_path;
};

StreamingRunResult RunOneStreamingPoint(StreamMatrixBench& bench, PointRunArgs args,
                                      std::shared_ptr<StreamingReceiverState> receiver,
                                      BenchInstant point_deadline);

}  // namespace ae::stream_matrix_bench

#endif

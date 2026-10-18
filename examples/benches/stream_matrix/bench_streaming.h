/*
 * Copyright 2026 Aethernet Inc.
 * Max-speed 1 MiB streaming benchmarks (TCP P2pStream, UDP P2pSafeStream).
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STREAMING_H_

namespace ae::stream_matrix_bench {

class StreamMatrixBench;

int RunStreamingTcpMatrix(StreamMatrixBench& bench);
int RunStreamingUdpSafeMatrix(StreamMatrixBench& bench);
void WriteStreamingComparison(std::filesystem::path const& output_dir);

}  // namespace ae::stream_matrix_bench

#endif

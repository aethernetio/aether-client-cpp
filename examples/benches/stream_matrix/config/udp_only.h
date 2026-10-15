/*
 * Stream matrix UDP measure configuration (include via USER_CONFIG).
 *
 * TCP is compiled in so registration/root-server bootstrap can use TCP endpoints.
 * Matrix modes are UDP-only; SetupMode restreams until VerifyP2pStreamTransport(UDP)
 * passes (no TCP on measured P2P send path).
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_UDP_ONLY_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_UDP_ONLY_H_

#define AE_SUPPORT_UDP 1
#define AE_SUPPORT_TCP 1
#define AE_STREAM_MATRIX_BENCH_UDP_MEASURE_ONLY 1

#endif

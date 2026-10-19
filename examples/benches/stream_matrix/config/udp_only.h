/*
 * Stream matrix UDP-only SDK configuration (include via USER_CONFIG).
 * Registration is performed by aether_stream_matrix_bootstrap (TCP SDK) into
 * persistent state/; this build loads clients without TCP in the SDK.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_UDP_ONLY_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_UDP_ONLY_H_

#define AE_SUPPORT_UDP 1
#define AE_SUPPORT_TCP 0
#define AE_STREAM_MATRIX_BENCH_UDP_MEASURE_ONLY 1

#undef AE_TELE_LOG_BLOB
#define AE_TELE_LOG_BLOB 0

#undef AE_EVENTS_MAX_COUNT
#define AE_EVENTS_MAX_COUNT 512

#endif

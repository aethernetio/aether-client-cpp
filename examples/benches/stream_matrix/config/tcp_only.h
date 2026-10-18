/*
 * Stream matrix TCP measure configuration (include via USER_CONFIG).
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_TCP_ONLY_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_CONFIG_TCP_ONLY_H_

#define AE_SUPPORT_UDP 0
#define AE_SUPPORT_TCP 1
#define AE_STREAM_MATRIX_BENCH_TCP_MEASURE_ONLY 1

/* Full packet blob logging during high-outstanding TCP writes overflows
 * tele format buffers (/GS, exit 0xC0000409). Bench metrics use run.log. */
#undef AE_TELE_LOG_BLOB
#define AE_TELE_LOG_BLOB 0

/* Two clients in one process register many events/handlers; each in-flight
 * WriteAction registers a status Event (~burst outstanding per TCP hop). */
#undef AE_EVENTS_MAX_COUNT
#define AE_EVENTS_MAX_COUNT 512

#endif

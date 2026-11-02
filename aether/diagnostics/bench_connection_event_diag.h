#ifndef AETHER_DIAGNOSTICS_BENCH_CONNECTION_EVENT_DIAG_H_
#define AETHER_DIAGNOSTICS_BENCH_CONNECTION_EVENT_DIAG_H_

#include "aether/config.h"

#if AE_BENCH_CONNECTION_EVENT_DIAG

#  include <cstdint>
#  include <cstddef>

namespace ae::bench_conn_event_diag {

std::uint64_t AllocateCloudServerConnectionInstanceId();
std::uint64_t AllocateClientServerConnectionInstanceId();
std::uint64_t AllocateServerConnectionInstanceId();

void Configure(std::size_t max_records);
void ResetForTest();

void Record(char const* event, std::uint32_t server_id,
            std::uint64_t cloud_conn_instance_id,
            std::uint64_t client_conn_instance_id, std::uint32_t ping_request_id,
            std::uint32_t original_timeout_ms, std::uint32_t applied_timeout_ms,
            int write_status, int error_code, std::int32_t main_server_id);

std::uint64_t DroppedCount();
void WriteTsv(char const* path);

}  // namespace ae::bench_conn_event_diag

#  define AE_BENCH_CONN_DIAG(...) \
    ::ae::bench_conn_event_diag::Record(__VA_ARGS__)

#else

#  define AE_BENCH_CONN_DIAG(...) ((void)0)

#endif

#endif

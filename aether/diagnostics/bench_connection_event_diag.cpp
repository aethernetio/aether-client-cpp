#include "aether/diagnostics/bench_connection_event_diag.h"

#if AE_BENCH_CONNECTION_EVENT_DIAG

#  include <atomic>
#  include <cstdio>
#  include <mutex>
#  include <vector>

#  if defined(_WIN32)
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <windows.h>
#  endif

namespace ae::bench_conn_event_diag {
namespace {

struct Row {
  std::int64_t qpc_ns{0};
  char const* event{nullptr};
  std::uint32_t server_id{0};
  std::uint64_t cloud_conn_instance_id{0};
  std::uint64_t client_conn_instance_id{0};
  std::uint32_t ping_request_id{0};
  std::uint32_t original_timeout_ms{0};
  std::uint32_t applied_timeout_ms{0};
  int write_status{-1};
  int error_code{0};
  std::int32_t main_server_id{-1};
};

std::atomic<std::uint64_t> g_cloud_id{1};
std::atomic<std::uint64_t> g_client_id{1};
std::atomic<std::uint64_t> g_server_id{1};
std::atomic<std::uint64_t> g_dropped{0};

std::mutex g_mu;
std::vector<Row> g_rows;
std::size_t g_max{65536};

std::int64_t QpcNowNs() {
#  if defined(_WIN32)
  LARGE_INTEGER c{};
  LARGE_INTEGER f{};
  if (::QueryPerformanceCounter(&c) == 0 ||
      ::QueryPerformanceFrequency(&f) == 0 || f.QuadPart == 0) {
    return 0;
  }
  std::int64_t const q = c.QuadPart;
  std::int64_t const fr = f.QuadPart;
  return (q / fr) * 1000000000LL + ((q % fr) * 1000000000LL) / fr;
#  else
  return 0;
#  endif
}

}  // namespace

std::uint64_t AllocateCloudServerConnectionInstanceId() {
  return g_cloud_id.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t AllocateClientServerConnectionInstanceId() {
  return g_client_id.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t AllocateServerConnectionInstanceId() {
  return g_server_id.fetch_add(1, std::memory_order_relaxed);
}

void Configure(std::size_t max_records) {
  std::lock_guard lock(g_mu);
  g_max = max_records > 0 ? max_records : 65536;
  g_rows.clear();
  g_rows.reserve(g_max);
  g_dropped.store(0, std::memory_order_relaxed);
}

void ResetForTest() { Configure(4096); }

void Record(char const* event, std::uint32_t server_id,
            std::uint64_t cloud_conn_instance_id,
            std::uint64_t client_conn_instance_id, std::uint32_t ping_request_id,
            std::uint32_t original_timeout_ms, std::uint32_t applied_timeout_ms,
            int write_status, int error_code, std::int32_t main_server_id) {
  Row r;
  r.qpc_ns = QpcNowNs();
  r.event = event;
  r.server_id = server_id;
  r.cloud_conn_instance_id = cloud_conn_instance_id;
  r.client_conn_instance_id = client_conn_instance_id;
  r.ping_request_id = ping_request_id;
  r.original_timeout_ms = original_timeout_ms;
  r.applied_timeout_ms = applied_timeout_ms;
  r.write_status = write_status;
  r.error_code = error_code;
  r.main_server_id = main_server_id;
  std::lock_guard lock(g_mu);
  if (g_rows.size() >= g_max) {
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_rows.push_back(r);
}

std::uint64_t DroppedCount() {
  return g_dropped.load(std::memory_order_relaxed);
}

void WriteTsv(char const* path) {
  std::vector<Row> copy;
  {
    std::lock_guard lock(g_mu);
    copy = g_rows;
  }
  FILE* f = nullptr;
#  if defined(_WIN32)
  if (fopen_s(&f, path, "w") != 0 || f == nullptr) {
    return;
  }
#  else
  f = std::fopen(path, "w");
  if (f == nullptr) {
    return;
  }
#  endif
  std::fprintf(
      f,
      "qpc_ns\tevent\tserver_id\tcloud_conn_instance_id\t"
      "client_conn_instance_id\tping_request_id\toriginal_timeout_ms\t"
      "applied_timeout_ms\twrite_status\terror_code\tmain_server_id\n");
  for (auto const& r : copy) {
    std::fprintf(f, "%lld\t%s\t%u\t%llu\t%llu\t%u\t%u\t%u\t%d\t%d\t%d\n",
                 static_cast<long long>(r.qpc_ns), r.event, r.server_id,
                 static_cast<unsigned long long>(r.cloud_conn_instance_id),
                 static_cast<unsigned long long>(r.client_conn_instance_id),
                 r.ping_request_id, r.original_timeout_ms, r.applied_timeout_ms,
                 r.write_status, r.error_code,
                 static_cast<int>(r.main_server_id));
  }
  std::fclose(f);
}

}  // namespace ae::bench_conn_event_diag

#endif

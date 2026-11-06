#if AE_DIAG_WRITE_FAIL_SOURCES

#include "aether/diagnostics/write_fail_diag.h"

#include <chrono>
#include <mutex>

namespace ae::write_fail_diag {

namespace {
std::mutex g_mu;
std::vector<Event> g_events;

std::int64_t NowUnixNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void SetLayer(Event& e, char const* layer) {
  if (layer == nullptr) {
    e.layer[0] = '\0';
    return;
  }
  std::size_t i = 0;
  for (; i + 1 < sizeof(e.layer) && layer[i] != '\0'; ++i) {
    e.layer[i] = layer[i];
  }
  e.layer[i] = '\0';
}
}  // namespace

thread_local std::optional<std::uint32_t> g_bench_open_loop_seq;

void Clear() noexcept {
  std::scoped_lock lock{g_mu};
  g_events.clear();
}

void Record(Source source, char const* layer, std::size_t fill,
            std::size_t capacity, std::optional<std::uint32_t> os_error) noexcept {
  Event e{};
  e.source = source;
  e.t_unix_ns = NowUnixNs();
  e.bench_seq = g_bench_open_loop_seq;
  e.fill = fill;
  e.capacity = capacity;
  e.os_error = os_error;
  SetLayer(e, layer);
  std::scoped_lock lock{g_mu};
  if (g_events.size() < 16384) {
    g_events.push_back(e);
  }
}

std::vector<Event> const& Events() noexcept {
  return g_events;
}

}  // namespace ae::write_fail_diag

#endif  // AE_DIAG_WRITE_FAIL_SOURCES

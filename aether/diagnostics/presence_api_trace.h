#ifndef AETHER_DIAGNOSTICS_PRESENCE_API_TRACE_H_
#define AETHER_DIAGNOSTICS_PRESENCE_API_TRACE_H_
#include <cstddef>
#include <cstdint>
#include "aether/config.h"
#if AE_BENCH_TCP_PROXY
#  include <cstdio>
#  include <cstdlib>
#endif
namespace ae {
inline void PresenceApiTrace(char const* event, std::int64_t a = 0,
                             std::int64_t b = 0, std::int64_t c = 0) {
#if AE_BENCH_TCP_PROXY
#  if defined(_MSC_VER)
  std::size_t trace_length{};
  getenv_s(&trace_length, nullptr, 0, "AE_PRESENCE_API_TRACE");
  auto const enabled = trace_length > 1;
#  else
  auto const enabled = std::getenv("AE_PRESENCE_API_TRACE") != nullptr;
#  endif
  if (enabled) {
    std::fprintf(stderr, "%s,%lld,%lld,%lld\n", event,
                 static_cast<long long>(a), static_cast<long long>(b),
                 static_cast<long long>(c));
  }
#else
  (void)event;
  (void)a;
  (void)b;
  (void)c;
#endif
}
}  // namespace ae
#endif

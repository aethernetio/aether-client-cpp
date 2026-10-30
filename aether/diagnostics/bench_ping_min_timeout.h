#ifndef AETHER_DIAGNOSTICS_BENCH_PING_MIN_TIMEOUT_H_
#define AETHER_DIAGNOSTICS_BENCH_PING_MIN_TIMEOUT_H_

#include "aether/clock.h"
#include "aether/config.h"

#include <chrono>

namespace ae::bench_ping_min_timeout {

inline std::uint32_t ChannelTimeoutMs(Duration channel_timeout) {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(channel_timeout)
          .count());
}

inline std::uint32_t AppliedTimeoutMs(std::uint32_t channel_timeout_ms) {
#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0
  if (channel_timeout_ms < static_cast<std::uint32_t>(AE_BENCH_PING_MIN_TIMEOUT_MS)) {
    return static_cast<std::uint32_t>(AE_BENCH_PING_MIN_TIMEOUT_MS);
  }
#endif
  return channel_timeout_ms;
}

inline Duration ApplyBenchPingMinTimeout(Duration channel_timeout) {
  return std::chrono::milliseconds(AppliedTimeoutMs(ChannelTimeoutMs(channel_timeout)));
}

}  // namespace ae::bench_ping_min_timeout

#endif

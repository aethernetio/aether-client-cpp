#ifndef AETHER_DIAGNOSTICS_BENCH_PING_SERVER_START_DEFER_H_
#define AETHER_DIAGNOSTICS_BENCH_PING_SERVER_START_DEFER_H_

#include "aether/config.h"

namespace ae::bench_ping_server_start {

#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0

struct DeferState {
  bool deferred_start_after_ping{false};
};

inline bool ShouldDeferPeriodicStart(bool has_ping, bool ping_is_pending) noexcept {
  return has_ping && ping_is_pending;
}

inline bool TryDeferPeriodicStart(DeferState& state, bool has_ping,
                                  bool ping_is_pending) noexcept {
  if (!ShouldDeferPeriodicStart(has_ping, ping_is_pending)) {
    return false;
  }
  state.deferred_start_after_ping = true;
  return true;
}

inline bool TakeDeferredStartAfterSuccess(DeferState& state) noexcept {
  if (!state.deferred_start_after_ping) {
    return false;
  }
  state.deferred_start_after_ping = false;
  return true;
}

inline void ClearDeferredStart(DeferState& state) noexcept {
  state.deferred_start_after_ping = false;
}

#else

struct DeferState {};

inline bool TryDeferPeriodicStart(DeferState&, bool, bool) noexcept {
  return false;
}

inline bool TakeDeferredStartAfterSuccess(DeferState&) noexcept { return false; }

inline void ClearDeferredStart(DeferState&) noexcept {}

#endif

}  // namespace ae::bench_ping_server_start

#endif

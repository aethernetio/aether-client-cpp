#ifndef AETHER_DIAGNOSTICS_BENCH_PING_SERVER_START_DEFER_H_
#define AETHER_DIAGNOSTICS_BENCH_PING_SERVER_START_DEFER_H_

#include "aether/config.h"

namespace ae::bench_ping_server_start {

struct DeferState {
  bool deferred_start_after_ping{false};
};

inline bool ShouldDeferPeriodicStart(bool has_ping, bool ping_is_pending,
                                     bool ping_awaiting_late) noexcept {
  return has_ping && (ping_is_pending || ping_awaiting_late);
}

inline bool TryDeferPeriodicStart(DeferState& state, bool has_ping,
                                  bool ping_is_pending,
                                  bool ping_awaiting_late) noexcept {
  if (!ShouldDeferPeriodicStart(has_ping, ping_is_pending,
                                ping_awaiting_late)) {
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

}  // namespace ae::bench_ping_server_start

#endif

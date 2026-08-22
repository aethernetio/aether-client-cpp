/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aether/cloud_connections/ping_bench_hooks.h"
#if AE_ENABLE_PING

#  include <chrono>

namespace ae {
namespace {

std::optional<PingTimingOverride> g_timing_override{};
PingBenchObserver g_observer{nullptr};

}  // namespace

void SetPingTimingOverride(std::optional<PingTimingOverride> override) {
  g_timing_override = std::move(override);
}

std::optional<PingTimingOverride> GetPingTimingOverride() noexcept {
  return g_timing_override;
}

void SetPingBenchObserver(PingBenchObserver observer) noexcept {
  g_observer = observer;
}

PingBenchObserver GetPingBenchObserver() noexcept { return g_observer; }

void EmitPingBenchEvent(PingBenchEvent event) noexcept {
  if (g_observer == nullptr) {
    return;
  }
  if (event.timestamp_steady_us == 0) {
    event.timestamp_steady_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
  }
  g_observer(event);
}

}  // namespace ae

#endif  // AE_ENABLE_PING

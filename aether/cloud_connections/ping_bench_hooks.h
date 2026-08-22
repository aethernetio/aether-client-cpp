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

#ifndef AETHER_CLOUD_CONNECTIONS_PING_BENCH_HOOKS_H_
#define AETHER_CLOUD_CONNECTIONS_PING_BENCH_HOOKS_H_

#include <chrono>
#include <cstdint>
#include <optional>

#include "aether/config.h"
#if AE_ENABLE_PING

#  include "aether/types/server_id.h"

namespace ae {

// Opt-in, process-local, non-persisted. Production leaves these unset.
struct PingTimingOverride {
  std::chrono::milliseconds actual_interval{};
  std::chrono::milliseconds announced_next{};
  std::chrono::milliseconds rx_window{};
};

enum class PingBenchEventKind : std::uint8_t {
  kPingScheduled = 0,
  kPingWriteBegin = 1,
  kPingServerAck = 2,
  kRxWindowOpen = 3,
  kRxWindowClose = 4,
  kNextPingScheduled = 5,
};

struct PingBenchEvent {
  PingBenchEventKind kind{};
  std::int64_t timestamp_steady_us{};
  ServerId server_id{};
  std::size_t priority{};
  std::int64_t actual_ping_interval_ms{};
  std::int64_t announced_next_ping_ms{};
  std::int64_t rx_window_ms{};
  std::uint32_t cycle_index{};
};

using PingBenchObserver = void (*)(PingBenchEvent const& event) noexcept;

// Process-local hooks for the delivery-window benchmark only.
void SetPingTimingOverride(std::optional<PingTimingOverride> override);
std::optional<PingTimingOverride> GetPingTimingOverride() noexcept;

void SetPingBenchObserver(PingBenchObserver observer) noexcept;
PingBenchObserver GetPingBenchObserver() noexcept;

void EmitPingBenchEvent(PingBenchEvent event) noexcept;

}  // namespace ae

#endif  // AE_ENABLE_PING
#endif  // AETHER_CLOUD_CONNECTIONS_PING_BENCH_HOOKS_H_

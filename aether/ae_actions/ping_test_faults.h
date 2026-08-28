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

#ifndef AETHER_AE_ACTIONS_PING_TEST_FAULTS_H_
#define AETHER_AE_ACTIONS_PING_TEST_FAULTS_H_

#include "aether/config.h"

#if AE_ENABLE_PING_TEST_FAULTS

#  include <chrono>
#  include <cstdint>
#  include <optional>
#  include <vector>

#  include "aether/clock.h"
#  include "aether/types/server_id.h"

namespace ae {

enum class PingFaultHarnessState : std::uint8_t {
  kIdle = 0,
  kArmed = 1,
  kMatched = 2,
  kDropped = 3,
  kConsumed = 4,
};

enum class PingFaultTraceKind : std::uint8_t {
  kCleared = 0,
  kArmed = 1,
  kBound = 2,
  kMatched = 3,
  kDropped = 4,
};

enum class PingFaultMode : std::uint8_t {
  kNone = 0,
  kDropRequest = 1,
  kIgnoreResponse = 2,
};

struct PingFaultTraceEvent {
  PingFaultTraceKind kind{PingFaultTraceKind::kCleared};
  ServerId server_id{};
  std::uint64_t logical_cycle_id{0};
  std::uint32_t physical_attempt_index{0};
  PingFaultMode mode{PingFaultMode::kNone};
  PingFaultHarnessState harness_state{PingFaultHarnessState::kIdle};
  std::int64_t steady_us{0};
};

using PingFaultTraceHook = void (*)(PingFaultTraceEvent const&);

struct PingFaultContext {
  ServerId server_id{};
  std::uint64_t logical_cycle_id{0};
  std::uint32_t physical_attempt_index{0};
  TimePoint planned_send_at{};
  TimePoint actual_send_at{};
};

struct PingFaultDecision {
  PingFaultMode mode{PingFaultMode::kNone};
  Duration timeout_override{};
};

struct PingFaultPlan {
  ServerId server_id{};
  std::uint64_t logical_cycle_id{0};  // 0 = next new cycle on this server
  std::uint32_t physical_attempt_index{1};
  PingFaultMode mode{PingFaultMode::kNone};
  Duration timeout_override{};
  bool consumed{false};
  // Test-only: delay this attempt until nominal_ping_at + offset.
  // Offset is send-time in the client clock, not a production schedule change.
  bool hold_enabled{false};
  std::int64_t retry_hold_offset_us{0};
};

class PingTestFaults {
 public:
  static PingTestFaults& Instance() noexcept {
    static PingTestFaults inst;
    return inst;
  }

  static void SetTraceHook(PingFaultTraceHook hook) noexcept {
    trace_hook_ = hook;
  }

  PingFaultHarnessState harness_state() const noexcept {
    return harness_state_;
  }

  bool HasUnconsumedPlan() const noexcept {
    for (auto const& plan : plans_) {
      if (!plan.consumed) {
        return true;
      }
    }
    return false;
  }

  void Clear() noexcept {
    plans_.clear();
    auth_ping_calls_ = 0;
    protocol_responses_ = 0;
    harness_state_ = PingFaultHarnessState::kIdle;
    EmitTrace(PingFaultTraceKind::kCleared, PingFaultPlan{}, 0, 0);
  }

  void Arm(PingFaultPlan plan) {
    plan.consumed = false;
    plans_.push_back(plan);
    harness_state_ = PingFaultHarnessState::kArmed;
    EmitTrace(PingFaultTraceKind::kArmed, plan, plan.logical_cycle_id,
              plan.physical_attempt_index);
  }

  void BindNextCycle(ServerId server_id, std::uint64_t cycle_id) noexcept {
    if (cycle_id == 0) {
      return;
    }
    for (auto& plan : plans_) {
      if (!plan.consumed && plan.server_id == server_id &&
          plan.logical_cycle_id == 0) {
        plan.logical_cycle_id = cycle_id;
        EmitTrace(PingFaultTraceKind::kBound, plan, cycle_id,
                  plan.physical_attempt_index);
      }
    }
  }

  std::optional<std::int64_t> RetryHoldOffsetUs(
      ServerId server_id, std::uint64_t cycle_id,
      std::uint32_t physical_attempt_index) const noexcept {
    for (auto const& plan : plans_) {
      if (plan.consumed || !plan.hold_enabled) {
        continue;
      }
      if (plan.server_id != server_id) {
        continue;
      }
      if (plan.logical_cycle_id != 0 && plan.logical_cycle_id != cycle_id) {
        continue;
      }
      if (plan.physical_attempt_index != physical_attempt_index) {
        continue;
      }
      return plan.retry_hold_offset_us;
    }
    return std::nullopt;
  }

  PingFaultDecision Consume(PingFaultContext const& ctx) noexcept {
    for (auto& plan : plans_) {
      if (plan.consumed) {
        continue;
      }
      if (plan.server_id != ctx.server_id) {
        continue;
      }
      if (plan.logical_cycle_id != 0 &&
          plan.logical_cycle_id != ctx.logical_cycle_id) {
        continue;
      }
      if (plan.physical_attempt_index != ctx.physical_attempt_index) {
        continue;
      }
      harness_state_ = PingFaultHarnessState::kMatched;
      EmitTrace(PingFaultTraceKind::kMatched, plan, ctx.logical_cycle_id,
                ctx.physical_attempt_index);
      plan.consumed = true;
      (void)ctx.planned_send_at;
      (void)ctx.actual_send_at;
      auto const decision =
          PingFaultDecision{plan.mode, plan.timeout_override};
      if (plan.mode == PingFaultMode::kDropRequest) {
        harness_state_ = PingFaultHarnessState::kDropped;
        EmitTrace(PingFaultTraceKind::kDropped, plan, ctx.logical_cycle_id,
                  ctx.physical_attempt_index);
      }
      harness_state_ = PingFaultHarnessState::kConsumed;
      return decision;
    }
    return PingFaultDecision{};
  }

  void OnAuthPing() noexcept { ++auth_ping_calls_; }
  void OnProtocolResponse() noexcept { ++protocol_responses_; }

  std::uint32_t auth_ping_calls() const noexcept { return auth_ping_calls_; }
  std::uint32_t protocol_responses() const noexcept {
    return protocol_responses_;
  }

 private:
  static inline PingFaultTraceHook trace_hook_{nullptr};

  static std::int64_t TraceSteadyUs() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  static void EmitTrace(PingFaultTraceKind kind, PingFaultPlan const& plan,
                        std::uint64_t logical_cycle_id,
                        std::uint32_t physical_attempt_index) noexcept {
    if (trace_hook_ == nullptr) {
      return;
    }
    PingFaultTraceEvent event{};
    event.kind = kind;
    event.server_id = plan.server_id;
    event.logical_cycle_id = logical_cycle_id;
    event.physical_attempt_index = physical_attempt_index;
    event.mode = plan.mode;
    event.harness_state = PingTestFaults::Instance().harness_state_;
    event.steady_us = TraceSteadyUs();
    trace_hook_(event);
  }

  std::vector<PingFaultPlan> plans_{};
  std::uint32_t auth_ping_calls_{0};
  std::uint32_t protocol_responses_{0};
  PingFaultHarnessState harness_state_{PingFaultHarnessState::kIdle};
};

inline void SetPingFaultTraceHook(PingFaultTraceHook hook) noexcept {
  PingTestFaults::SetTraceHook(hook);
}

}  // namespace ae

#endif  // AE_ENABLE_PING_TEST_FAULTS
#endif  // AETHER_AE_ACTIONS_PING_TEST_FAULTS_H_

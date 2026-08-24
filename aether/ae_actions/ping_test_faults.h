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

#  include <cstdint>
#  include <vector>

#  include "aether/clock.h"
#  include "aether/types/server_id.h"

namespace ae {

enum class PingFaultMode : std::uint8_t {
  kNone = 0,
  kDropRequest = 1,
  kIgnoreResponse = 2,
};

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
};

class PingTestFaults {
 public:
  static PingTestFaults& Instance() noexcept {
    static PingTestFaults inst;
    return inst;
  }

  void Clear() noexcept {
    plans_.clear();
    auth_ping_calls_ = 0;
    protocol_responses_ = 0;
  }

  void Arm(PingFaultPlan plan) {
    plan.consumed = false;
    plans_.push_back(plan);
  }

  void BindNextCycle(ServerId server_id, std::uint64_t cycle_id) noexcept {
    if (cycle_id == 0) {
      return;
    }
    for (auto& plan : plans_) {
      if (!plan.consumed && plan.server_id == server_id &&
          plan.logical_cycle_id == 0) {
        plan.logical_cycle_id = cycle_id;
      }
    }
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
      plan.consumed = true;
      (void)ctx.planned_send_at;
      (void)ctx.actual_send_at;
      return PingFaultDecision{plan.mode, plan.timeout_override};
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
  std::vector<PingFaultPlan> plans_{};
  std::uint32_t auth_ping_calls_{0};
  std::uint32_t protocol_responses_{0};
};

}  // namespace ae

#endif  // AE_ENABLE_PING_TEST_FAULTS
#endif  // AETHER_AE_ACTIONS_PING_TEST_FAULTS_H_

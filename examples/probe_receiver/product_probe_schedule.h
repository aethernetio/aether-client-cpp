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

#ifndef EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SCHEDULE_H_
#define EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SCHEDULE_H_

#include <cstdint>

// Product policy: first temperature send immediately; adaptive probe only after
// ~1 hour of successful operation. RTC-retained elapsed time, no flash writes.

namespace ae::probe_schedule {

static constexpr std::uint32_t kProbeDelayUs = 3600u * 1000000u;

enum class FirstSendDecision : std::uint8_t {
  kSendTemperatureNow = 0,
  kDeferProbeNotDue = 1,
  kRunAdaptiveProbe = 2,
};

struct RtcScheduleState {
  std::uint32_t magic{0};
  std::uint32_t first_success_us{0};
  std::uint32_t last_awake_us{0};
  std::uint8_t first_send_done{0};
  std::uint8_t probe_completed{0};
};

static constexpr std::uint32_t kRtcMagic = 0x50534D31u;  // PSM1

inline bool RtcStateValid(RtcScheduleState const& s) {
  return s.magic == kRtcMagic;
}

// Returns whether adaptive probe may run on this boot.
inline FirstSendDecision DecideOnWake(RtcScheduleState* state,
                                      std::uint32_t now_us,
                                      bool cold_boot) {
  if (state == nullptr) {
    return FirstSendDecision::kSendTemperatureNow;
  }
  if (cold_boot || !RtcStateValid(*state)) {
    state->magic = kRtcMagic;
    state->first_success_us = 0;
    state->last_awake_us = now_us;
    state->first_send_done = 0;
    state->probe_completed = 0;
    return FirstSendDecision::kSendTemperatureNow;
  }
  if (state->probe_completed != 0) {
    return FirstSendDecision::kDeferProbeNotDue;
  }
  if (state->first_send_done == 0) {
    return FirstSendDecision::kSendTemperatureNow;
  }
  if (state->first_success_us == 0) {
    return FirstSendDecision::kSendTemperatureNow;
  }
  if (now_us - state->first_success_us >= kProbeDelayUs) {
    return FirstSendDecision::kRunAdaptiveProbe;
  }
  return FirstSendDecision::kDeferProbeNotDue;
}

inline void RecordFirstTemperatureSuccess(RtcScheduleState* state,
                                          std::uint32_t now_us) {
  if (state == nullptr) {
    return;
  }
  state->magic = kRtcMagic;
  if (state->first_send_done == 0) {
    state->first_send_done = 1;
    state->first_success_us = now_us;
  }
  state->last_awake_us = now_us;
}

inline void RecordProbeCompleted(RtcScheduleState* state,
                                 std::uint32_t now_us) {
  if (state == nullptr) {
    return;
  }
  state->probe_completed = 1;
  state->last_awake_us = now_us;
}

inline std::uint32_t RemainingProbeDelayUs(RtcScheduleState const& state,
                                           std::uint32_t now_us) {
  if (!RtcStateValid(state) || state.first_success_us == 0) {
    return kProbeDelayUs;
  }
  if (now_us <= state.first_success_us) {
    return kProbeDelayUs;
  }
  auto const elapsed = now_us - state.first_success_us;
  return elapsed >= kProbeDelayUs ? 0u : (kProbeDelayUs - elapsed);
}

}  // namespace ae::probe_schedule

#endif  // EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SCHEDULE_H_

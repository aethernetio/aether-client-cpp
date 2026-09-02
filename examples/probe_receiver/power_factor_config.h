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

#ifndef EXAMPLES_PROBE_RECEIVER_POWER_FACTOR_CONFIG_H_
#define EXAMPLES_PROBE_RECEIVER_POWER_FACTOR_CONFIG_H_

#include <cstddef>
#include <cstdint>

// Shared between ESP bench firmware, desktop receiver and host unit tests.
// Each hardware run uses exactly one variant id baked in at compile time.

namespace ae::power_bench {

#ifndef AETHER_POWER_BENCH_HOT_ATTEMPTS
#  define AETHER_POWER_BENCH_HOT_ATTEMPTS 100
#endif
#ifndef AETHER_POWER_BENCH_HOT_SLEEP_MS
#  define AETHER_POWER_BENCH_HOT_SLEEP_MS 2000
#endif
#ifndef AETHER_POWER_BENCH_MIN_RX_UNIQUE
#  define AETHER_POWER_BENCH_MIN_RX_UNIQUE 90
#endif

static constexpr std::uint16_t kHotAttempts =
    static_cast<std::uint16_t>(AETHER_POWER_BENCH_HOT_ATTEMPTS);
static constexpr std::uint16_t kHotSleepMs =
    static_cast<std::uint16_t>(AETHER_POWER_BENCH_HOT_SLEEP_MS);
static constexpr std::uint16_t kMinRxUnique =
    static_cast<std::uint16_t>(AETHER_POWER_BENCH_MIN_RX_UNIQUE);

enum class VariantId : std::uint16_t {
  kA0Clean = 0,
  kA0Repeat = 1,
  kB1SkipValidateDeepSleep = 10,
  kB2DiscPmOff = 11,
  kB3WifiPsMin = 12,
  kB4WifiPsMaxLi1 = 13,
  kB5WifiPsMaxLi3 = 14,
  kB6Cpu120 = 15,
  kB7Cpu80 = 16,
  kB8Cpu40 = 17,
  kB9PmfOff = 18,
  kB10EncodeDuringAssociation = 19,
  kB11WifiStopOnly = 20,
  kB12DirectDeepSleep = 21,
  kB13PhyPartialEveryWake = 22,
  // Phase C interaction variants (chirkov first).
  kC1CpuMin = 100,
  kC2CpuMaxLi1 = 101,
  kC3AlsNone = 110,
  kC4AlsMin = 111,
  kC5AlsMaxLi1 = 112,
  kC6PreMinToNone = 120,
  kC7PreMaxToNone = 121,
  kC8EncodeCpu80 = 130,
  kC9PmfOffCpu80 = 131,
  kC10TeardownMatrix = 140,
  kC11PhyFinal = 150,
  // aethernetio subset.
  kIoA0 = 200,
  kIoDiscPmOff = 201,
  kIoBestCpu = 202,
  kIoBestPs = 203,
  kIoBestDfs = 204,
  kIoBestOverall = 205,
  kIoTeardown = 206,
  kIoPmfOff = 207,
  kIoPhy = 208,
  kCount
};

struct VariantSpec {
  VariantId id;
  char const* name;
  char const* changed_factor;
  char const* phase;  // A, B, C, IO
};

inline VariantSpec const* LookupVariant(VariantId id) {
  static VariantSpec const kTable[] = {
      {VariantId::kA0Clean, "A0_CLEAN", "control", "A"},
      {VariantId::kA0Repeat, "A0_REPEAT", "repeat control", "A"},
      {VariantId::kB1SkipValidateDeepSleep, "B1_SKIP_VALIDATE_DEEP_SLEEP",
       "boot validation skip in deep sleep", "B"},
      {VariantId::kB2DiscPmOff, "B2_DISC_PM_OFF", "disconnected PM off", "B"},
      {VariantId::kB3WifiPsMin, "B3_WIFI_PS_MIN", "connected MIN modem PS", "B"},
      {VariantId::kB4WifiPsMaxLi1, "B4_WIFI_PS_MAX_LI1",
       "connected MAX modem PS LI=1", "B"},
      {VariantId::kB5WifiPsMaxLi3, "B5_WIFI_PS_MAX_LI3",
       "connected MAX modem PS LI=3", "B"},
      {VariantId::kB6Cpu120, "B6_CPU120", "CPU 120 MHz fixed", "B"},
      {VariantId::kB7Cpu80, "B7_CPU80", "CPU 80 MHz fixed", "B"},
      {VariantId::kB8Cpu40, "B8_CPU40", "CPU 40 MHz fixed", "B"},
      {VariantId::kB9PmfOff, "B9_PMF_OFF", "PMF off", "B"},
      {VariantId::kB10EncodeDuringAssociation, "B10_ENCODE_DURING_ASSOCIATION",
       "encode during association", "B"},
      {VariantId::kB11WifiStopOnly, "B11_WIFI_STOP_ONLY", "teardown stop only",
       "B"},
      {VariantId::kB12DirectDeepSleep, "B12_DIRECT_DEEP_SLEEP",
       "direct deep sleep", "B"},
      {VariantId::kB13PhyPartialEveryWake, "B13_PHY_PARTIAL_EVERY_WAKE",
       "PHY partial every wake", "B"},
  };
  for (auto const& row : kTable) {
    if (row.id == id) {
      return &row;
    }
  }
  return nullptr;
}

inline char const* VariantName(VariantId id) {
  auto const* spec = LookupVariant(id);
  return spec != nullptr ? spec->name : "UNKNOWN";
}

}  // namespace ae::power_bench

#endif  // EXAMPLES_PROBE_RECEIVER_POWER_FACTOR_CONFIG_H_

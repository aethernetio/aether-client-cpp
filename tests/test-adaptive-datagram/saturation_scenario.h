/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef TESTS_TEST_ADAPTIVE_DATAGRAM_SATURATION_SCENARIO_H_
#define TESTS_TEST_ADAPTIVE_DATAGRAM_SATURATION_SCENARIO_H_

#include <cstddef>
#include <cstdint>

#include "aether/clock.h"

namespace ae::saturation {

enum class ArrivalPattern { kPeriodic, kPoisson };

struct Config {
  std::uint64_t capacity_Bps{100000};
  std::size_t packet_bytes{800};
  // Includes the packet currently being serialized by the existing emulator.
  std::size_t queue_packets{8};
  Duration propagation_delay{10ms};
  std::uint32_t load_per_mille{900};
  std::uint64_t seed{1};
  unsigned warmup_ms{10000};
  unsigned measure_ms{50000};
  ArrivalPattern pattern{ArrivalPattern::kPoisson};
};

struct Result {
  std::uint64_t offered_packets{};
  std::uint64_t received_packets{};
  std::uint64_t queue_drops{};
  std::uint64_t random_drops{};
  std::uint64_t other_drops{};
  std::uint64_t offered_bytes{};
  std::uint64_t received_bytes{};
  std::uint32_t delay_p50_us{};
  std::uint32_t delay_p95_us{};
  std::uint32_t delay_max_us{};
  double offered_Bps{};
  double goodput_Bps{};
  double loss_pct{};
  std::size_t peak_queue_bytes{};
  bool accounting_ok{};
  bool local_writes_ok{};
};

// Open-loop arrivals into the production ISocket emulator. Capacity and buffer
// stay fixed and all configured random impairments are zero: loss can only
// arise from the finite serialization queue. Measurements select the SEND
// cohort in [warmup, warmup + measure), then drain the entire network. Thus
// goodput_Bps is delivered cohort bytes / measurement duration; the drain time
// is not included in that denominator. No AdaptiveDatagramStream controls load.
// Repeatability is for the same executable/platform (Poisson uses std::log).
Result Run(Config const& config);

}  // namespace ae::saturation
#endif  // TESTS_TEST_ADAPTIVE_DATAGRAM_SATURATION_SCENARIO_H_

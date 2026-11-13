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

#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "saturation_scenario.h"

namespace ae::test_saturation {
constexpr std::array<std::uint64_t, 5> kSeeds{12345, 24680, 7, 42, 999};

struct Aggregate {
  std::uint64_t sent{};
  std::uint64_t drops{};
  double p95_us{};
  double goodput_Bps{};
  double LossPercent() const {
    return 100.0 * static_cast<double>(drops) / static_cast<double>(sent);
  }
};

Aggregate Measure(saturation::Config config) {
  Aggregate total;
  for (auto seed : kSeeds) {
    config.seed = seed;
    auto const result = saturation::Run(config);
    TEST_ASSERT_TRUE(result.accounting_ok);
    TEST_ASSERT_TRUE(result.local_writes_ok);
    TEST_ASSERT_EQUAL(0, result.random_drops);
    TEST_ASSERT_EQUAL(0, result.other_drops);
    TEST_ASSERT_TRUE(result.peak_queue_bytes <=
                     config.queue_packets * config.packet_bytes);
    auto const maximum_delay = config.propagation_delay.count() +
                               config.queue_packets * config.packet_bytes *
                                   1000000ULL / config.capacity_Bps;
    TEST_ASSERT_TRUE(result.delay_max_us <= maximum_delay + 1);
    total.sent += result.offered_packets;
    total.drops += result.queue_drops;
    total.p95_us += result.delay_p95_us;
    total.goodput_Bps += result.goodput_Bps;
  }
  total.p95_us /= kSeeds.size();
  total.goodput_Bps /= kSeeds.size();
  return total;
}

void test_BurstyLoadNaturallyRaisesDelayAndLossNearCapacity() {
  saturation::Config config;
  config.load_per_mille = 500;
  auto const low = Measure(config);
  config.load_per_mille = 850;
  auto const approaching = Measure(config);
  config.load_per_mille = 950;
  auto const near = Measure(config);
  config.load_per_mille = 1250;
  auto const overloaded = Measure(config);
  config.load_per_mille = 1500;
  auto const severe = Measure(config);

  TEST_ASSERT_TRUE(low.LossPercent() < 0.5);
  TEST_ASSERT_TRUE(near.LossPercent() > low.LossPercent() + 2.0);
  TEST_ASSERT_TRUE(approaching.p95_us > low.p95_us + 10000);
  TEST_ASSERT_TRUE(near.p95_us > low.p95_us + 20000);
  TEST_ASSERT_TRUE(overloaded.LossPercent() > near.LossPercent() + 8.0);
  TEST_ASSERT_TRUE(severe.LossPercent() > overloaded.LossPercent() + 8.0);
  // Increasing offered rate beyond capacity creates drops, not extra capacity.
  TEST_ASSERT_TRUE(overloaded.goodput_Bps >= 95000);
  TEST_ASSERT_TRUE(severe.goodput_Bps <= 101000);
  TEST_ASSERT_TRUE(severe.goodput_Bps < overloaded.goodput_Bps * 1.05);
}

void test_PeriodicBelowCapacityHasNoArtificialDelayOrLoss() {
  saturation::Config config;
  config.pattern = saturation::ArrivalPattern::kPeriodic;
  for (auto load : {500U, 950U, 1000U}) {
    config.load_per_mille = load;
    auto const result = saturation::Run(config);
    TEST_ASSERT_TRUE(result.accounting_ok);
    TEST_ASSERT_EQUAL(0, result.queue_drops);
    TEST_ASSERT_EQUAL(18000, result.delay_p50_us);
    TEST_ASSERT_EQUAL(18000, result.delay_p95_us);
  }
}

void test_CapacityAndQueueSizeExplainTheObservedEffects() {
  saturation::Config config;
  config.load_per_mille = 950;
  auto const original = Measure(config);
  config.capacity_Bps = 200000;
  config.load_per_mille = 475;  // Same absolute offered rate: 95,000 B/s.
  auto const faster = Measure(config);
  TEST_ASSERT_TRUE(faster.LossPercent() < original.LossPercent() / 4);
  TEST_ASSERT_TRUE(faster.p95_us < original.p95_us / 2);

  config.capacity_Bps = 100000;
  config.load_per_mille = 950;
  config.queue_packets = 16;
  auto const larger_buffer = Measure(config);
  TEST_ASSERT_TRUE(larger_buffer.LossPercent() < original.LossPercent());
  TEST_ASSERT_TRUE(larger_buffer.p95_us > original.p95_us + 20000);
}

void test_ReducingLoadDrainsTheSameQueue() {
  EmulatedDatagramNetwork network{12345};
  EmulatedDatagramSocket source{network, 0};
  EmulatedDatagramSocket receiver{network, 1};
  source.Connect(AddressPort{}, {});
  receiver.Connect(AddressPort{}, {});
  EmulatedDatagramProfile profile;
  profile.bytes_per_second = 100000;
  profile.queue_bytes = 6400;
  profile.delay = 10ms;
  network.SetProfile(0, profile);
  unsigned steady_received = 0;
  receiver.RecvData([&](Span<std::uint8_t> data) {
    std::uint64_t sent_us{};
    std::memcpy(&sent_us, data.data(), sizeof(sent_us));
    if (sent_us >= 2200000) {
      auto const arrival_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              network.now().time_since_epoch())
              .count();
      TEST_ASSERT_TRUE(static_cast<std::uint64_t>(arrival_us) - sent_us ==
                       18000);
      ++steady_received;
    }
  });
  auto send_at = [&](std::uint64_t at_us) {
    network.AdvanceTo(TimePoint{} + std::chrono::microseconds{at_us});
    DataBuffer data(800);
    std::memcpy(data.data(), &at_us, sizeof(at_us));
    auto const result = source.Send(Span{data.data(), data.size()});
    TEST_ASSERT_TRUE(result.has_value() && *result == data.size());
  };
  for (std::uint64_t time = 0; time < 2000000; time += 5000) {
    send_at(time);
  }
  TEST_ASSERT_TRUE(network.stats(0).dropped_queue > 0);
  unsigned expected_steady = 0;
  for (std::uint64_t time = 2000000; time < 3000000; time += 16000) {
    auto const before = network.stats(0).dropped_queue;
    send_at(time);
    if (time >= 2200000) {
      ++expected_steady;
      TEST_ASSERT_TRUE(network.stats(0).dropped_queue == before);
    }
  }
  network.AdvanceTo(TimePoint{} + 3100ms);
  TEST_ASSERT_EQUAL(expected_steady, steady_received);
}
}  // namespace ae::test_saturation

int test_saturation() {
  using namespace ae::test_saturation;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(test_BurstyLoadNaturallyRaisesDelayAndLossNearCapacity);
  RUN_TEST(test_PeriodicBelowCapacityHasNoArtificialDelayOrLoss);
  RUN_TEST(test_CapacityAndQueueSizeExplainTheObservedEffects);
  RUN_TEST(test_ReducingLoadDrainsTheSameQueue);
  return UNITY_END();
}

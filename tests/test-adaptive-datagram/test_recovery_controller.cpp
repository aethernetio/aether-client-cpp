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

#include <cstddef>
#include <cstdint>

#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/adaptive_datagram/rate_controller.h"
#include "aether/clock.h"

namespace ae::test_recovery_controller {
using adaptive_datagram::Config;
using adaptive_datagram::RateController;

TimePoint At(std::uint32_t milliseconds) {
  return TimePoint{} + std::chrono::milliseconds{milliseconds};
}

void LossCohort(RateController& controller, std::uint32_t completed_ms,
                std::uint32_t sent_ms, std::size_t count = 20) {
  for (std::size_t i = 0; i < count; ++i) {
    controller.RecordOutcome(At(completed_ms), 800, false, Duration{20ms},
                             At(sent_ms));
  }
  controller.FeedbackReceived(At(completed_ms), Duration{40ms});
  controller.Update(At(completed_ms), Duration::zero());
}

void test_DelayedLossCohortCannotPunishTheNewRate() {
  Config config;
  RateController controller{config, At(0)};
  LossCohort(controller, 100, 50);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);

  // These results arrive later, but the packets were already in flight before
  // the rate changed. Retain their loss in W without treating the old batch as
  // evidence that the reduced rate is still too high.
  LossCohort(controller, 200, 60);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 40);
  TEST_ASSERT_TRUE(controller.metrics().total_lost_packets == 40);
  TEST_ASSERT_EQUAL_UINT32(1000, controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);

  // A packet genuinely sent at the reduced rate may initiate a new response.
  LossCohort(controller, 300, 220, 1);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 41);
}

void test_RepeatedOldLatencyDoesNotReapplyBackoff() {
  Config config;
  RateController controller{config, At(0)};
  controller.FeedbackReceived(At(100), Duration{40ms});
  controller.ObserveLatency(Duration{150ms}, At(50));
  controller.Update(At(100), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);

  controller.FeedbackReceived(At(200), Duration{40ms});
  controller.ObserveLatency(Duration{150ms}, At(60));
  controller.Update(At(200), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);

  controller.FeedbackReceived(At(300), Duration{40ms});
  controller.ObserveLatency(Duration{150ms}, At(220));
  controller.Update(At(300), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
}

void test_LossWindowStillUsesCompletionTimeAfterEpochFiltering() {
  Config config;
  config.loss_window = Duration{300ms};
  RateController controller{config, At(0)};
  LossCohort(controller, 100, 50);
  LossCohort(controller, 350, 60);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 40);

  controller.FeedbackReceived(At(400), Duration{40ms});
  controller.Update(At(400), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 20);
  TEST_ASSERT_EQUAL_UINT32(1000, controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);

  controller.FeedbackReceived(At(650), Duration{40ms});
  controller.Update(At(650), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 0);
  TEST_ASSERT_FALSE(controller.metrics().window_valid);
  TEST_ASSERT_FALSE(controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
}

void test_FreshEpochBackoffsRespectConfiguredRateFloor() {
  Config config;
  config.min_rate_bytes_per_second = 40000;
  config.max_rate_bytes_per_second = 64000;
  RateController controller{config, At(0)};
  LossCohort(controller, 100, 50);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  LossCohort(controller, 200, 150);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
  LossCohort(controller, 300, 250);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40000);
  LossCohort(controller, 400, 350);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40000);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
}

void test_DemandLimitedDeliveryEstimateDoesNotCapAHealthyPath() {
  Config config;
  RateController controller{config, At(0)};
  for (std::uint32_t now = 100; now <= 200; now += 100) {
    controller.ObserveDeliveryRate(At(now), 1000);
    for (int i = 0; i < 20; ++i) {
      controller.RecordOutcome(At(now), 800, true, Duration{20ms},
                               At(now - 50));
    }
    controller.FeedbackReceived(At(now), Duration{40ms});
    controller.Update(At(now), Duration::zero());
  }
  TEST_ASSERT_TRUE(controller.metrics().delivery_rate == 1000);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() >
                   config.initial_rate_bytes_per_second);
  TEST_ASSERT_FALSE(controller.metrics().constraints_unmet);
}

void test_ConfirmedCongestionCanUseFreshDeliveryRateToDrain() {
  Config config;
  RateController controller{config, At(0)};
  controller.ObserveDeliveryRate(At(100), 20000);
  LossCohort(controller, 100, 50);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 19000);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
}

void test_StaleOrPreviousPathDeliveryEstimateCannotCapNewRate() {
  Config config;
  RateController controller{config, At(0)};
  controller.ObserveDeliveryRate(At(0), 20000);
  // The estimate is older than feedback_timeout, despite a fresh report now.
  LossCohort(controller, 600, 550);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);

  controller.ObserveDeliveryRate(At(700), 1000);
  controller.ResetPath(At(800));
  TEST_ASSERT_TRUE(controller.metrics().delivery_rate == 0);
  LossCohort(controller, 900, 850);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
}

void test_PreviousPathOriginsCannotBackOffTheResetPath() {
  Config config;
  RateController controller{config, At(0)};
  LossCohort(controller, 100, 50);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  controller.ResetPath(At(500));
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);

  LossCohort(controller, 600, 450);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  controller.FeedbackReceived(At(700), Duration{40ms});
  controller.ObserveLatency(Duration{150ms}, At(450));
  controller.Update(At(700), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);

  // Real evidence from the current path still has authority after the reset.
  LossCohort(controller, 800, 750);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
}
}  // namespace ae::test_recovery_controller

int test_recovery_controller() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_recovery_controller::
               test_DelayedLossCohortCannotPunishTheNewRate);
  RUN_TEST(ae::test_recovery_controller::
               test_RepeatedOldLatencyDoesNotReapplyBackoff);
  RUN_TEST(ae::test_recovery_controller::
               test_LossWindowStillUsesCompletionTimeAfterEpochFiltering);
  RUN_TEST(ae::test_recovery_controller::
               test_FreshEpochBackoffsRespectConfiguredRateFloor);
  RUN_TEST(ae::test_recovery_controller::
               test_DemandLimitedDeliveryEstimateDoesNotCapAHealthyPath);
  RUN_TEST(ae::test_recovery_controller::
               test_ConfirmedCongestionCanUseFreshDeliveryRateToDrain);
  RUN_TEST(ae::test_recovery_controller::
               test_StaleOrPreviousPathDeliveryEstimateCannotCapNewRate);
  RUN_TEST(ae::test_recovery_controller::
               test_PreviousPathOriginsCannotBackOffTheResetPath);
  return UNITY_END();
}

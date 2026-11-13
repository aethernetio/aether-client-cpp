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
#include <limits>

#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/adaptive_datagram/rate_controller.h"
#include "aether/clock.h"

namespace ae::test_rate_controller {

using adaptive_datagram::Config;
using adaptive_datagram::RateController;

TimePoint At(std::uint32_t milliseconds) {
  return TimePoint{} + std::chrono::milliseconds{milliseconds};
}

void RecordBatch(RateController& controller, std::uint32_t milliseconds,
                 std::size_t delivered, std::size_t lost = 0,
                 Duration latency = Duration{10ms}) {
  for (std::size_t i = 0; i < delivered; ++i) {
    controller.RecordOutcome(At(milliseconds), 100, true, latency);
  }
  for (std::size_t i = 0; i < lost; ++i) {
    controller.RecordOutcome(At(milliseconds), 100, false, latency);
  }
  controller.FeedbackReceived(At(milliseconds), Duration{20ms});
  controller.Update(At(milliseconds), Duration::zero());
}

void test_ValidateConfiguration() {
  Config config;
  TEST_ASSERT_TRUE(config.Validate());
  config.peer_session_id = config.session_id;
  TEST_ASSERT_TRUE(config.Validate());
  config.max_loss_per_mille = 1000;
  TEST_ASSERT_TRUE(config.Validate());
  config.max_loss_per_mille = 1001;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.loss_window = Duration::zero();
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.latency_target = Duration::zero();
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.feedback_timeout = config.feedback_interval;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.control_interval = Duration::zero();
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.min_rate_bytes_per_second = config.initial_rate_bytes_per_second + 1;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.max_datagram_size = 255;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.max_in_flight_packets = 513;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.max_history_packets = config.min_samples - 1;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.max_queue_packets = 0;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.session_id = 0;
  TEST_ASSERT_FALSE(config.Validate());
  config = Config{};
  config.max_rate_bytes_per_second = std::numeric_limits<std::uint64_t>::max();
  TEST_ASSERT_FALSE(config.Validate());
}

void test_EmptyWindowAndFeedbackAreNotDeliveryEvidence() {
  Config config;
  RateController controller{config, At(0)};
  controller.FeedbackReceived(At(100), Duration{20ms});
  controller.Update(At(100), Duration::zero());
  controller.FeedbackReceived(At(200), Duration{20ms});
  controller.Update(At(200), Duration::zero());
  TEST_ASSERT_FALSE(controller.metrics().window_valid);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 0);
  TEST_ASSERT_TRUE(controller.metrics().delivered_bytes == 0);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() ==
                   config.initial_rate_bytes_per_second);
}

void test_HealthyProbingRequiresFreshSamplesAndHysteresis() {
  Config config;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 40);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  RecordBatch(controller, 200, 40);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 67200);
  controller.FeedbackReceived(At(300), Duration{20ms});
  controller.Update(At(300), Duration::zero());
  controller.FeedbackReceived(At(400), Duration{20ms});
  controller.Update(At(400), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 67200);
  TEST_ASSERT_TRUE(controller.metrics().window_valid);
  TEST_ASSERT_TRUE(controller.metrics().delivered_bytes == 8000);
}

void test_LossTargetChangesRateDecision() {
  Config strict;
  Config tolerant = strict;
  tolerant.max_loss_per_mille = 200;
  RateController strict_controller{strict, At(0)};
  RateController tolerant_controller{tolerant, At(0)};
  RecordBatch(strict_controller, 100, 18, 2);
  RecordBatch(tolerant_controller, 100, 18, 2);
  TEST_ASSERT_EQUAL_UINT32(100,
                           strict_controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(strict_controller.metrics().constraints_unmet);
  TEST_ASSERT_FALSE(tolerant_controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(strict_controller.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(tolerant_controller.rate_bytes_per_second() == 64000);
}

void test_OldLossSamplesDoNotRepeatedlyCollapseRate() {
  Config config;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 0, 40);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  controller.FeedbackReceived(At(200), Duration{20ms});
  controller.Update(At(200), Duration::zero());
  controller.FeedbackReceived(At(300), Duration{20ms});
  controller.Update(At(300), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
}

void test_SuccessesDoNotReapplyOldLossReduction() {
  Config config;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 80, 20);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  RecordBatch(controller, 200, 20);
  RecordBatch(controller, 300, 20);
  TEST_ASSERT_TRUE(controller.metrics().window_loss_per_mille >
                   config.max_loss_per_mille);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  // A genuinely new lost datagram can trigger the next congestion response.
  RecordBatch(controller, 400, 20, 1);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
}

void test_TemporalWindowExpiresAtExactBoundary() {
  Config config;
  config.min_samples = 1;
  RateController controller{config, At(0)};
  controller.RecordOutcome(At(0), 100, false, Duration{10ms});
  controller.RecordOutcome(At(500), 100, true, Duration{10ms});
  controller.FeedbackReceived(At(999), Duration{20ms});
  controller.Update(At(999), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 2);
  controller.Update(At(1000), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 1);
  TEST_ASSERT_EQUAL_UINT32(0, controller.metrics().window_loss_per_mille);
  controller.Update(At(1500), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 0);
  TEST_ASSERT_FALSE(controller.metrics().window_valid);
}

void test_WindowLengthChangesHistoryAndDecision() {
  Config short_window;
  short_window.loss_window = Duration{200ms};
  short_window.min_samples = 1;
  Config long_window = short_window;
  long_window.loss_window = Duration{1s};
  RateController short_controller{short_window, At(0)};
  RateController long_controller{long_window, At(0)};
  short_controller.RecordOutcome(At(10), 100, false, Duration{10ms});
  long_controller.RecordOutcome(At(10), 100, false, Duration{10ms});
  RecordBatch(short_controller, 300, 1);
  RecordBatch(long_controller, 300, 1);
  TEST_ASSERT_EQUAL_UINT32(0, short_controller.metrics().window_loss_per_mille);
  TEST_ASSERT_EQUAL_UINT32(500,
                           long_controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(short_controller.rate_bytes_per_second() == 64000);
  TEST_ASSERT_TRUE(long_controller.rate_bytes_per_second() == 51200);
}

void test_LatencyTargetProtectsBeforeFinalOutcome() {
  Config strict;
  strict.latency_target = Duration{50ms};
  Config tolerant = strict;
  tolerant.latency_target = Duration{200ms};
  RateController strict_controller{strict, At(0)};
  RateController tolerant_controller{tolerant, At(0)};
  strict_controller.ObserveLatency(Duration{100ms});
  tolerant_controller.ObserveLatency(Duration{100ms});
  strict_controller.FeedbackReceived(At(100), Duration{200ms});
  tolerant_controller.FeedbackReceived(At(100), Duration{200ms});
  strict_controller.Update(At(100), Duration::zero());
  tolerant_controller.Update(At(100), Duration::zero());
  TEST_ASSERT_TRUE(strict_controller.metrics().window_samples == 0);
  TEST_ASSERT_TRUE(strict_controller.metrics().constraints_unmet);
  TEST_ASSERT_FALSE(tolerant_controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(strict_controller.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(tolerant_controller.rate_bytes_per_second() == 64000);
  strict_controller.Update(At(200), Duration::zero());
  TEST_ASSERT_TRUE(strict_controller.rate_bytes_per_second() == 51200);
}

void test_QueueGrowthIsRateLimitedByControlInterval() {
  Config config;
  RateController controller{config, At(0)};
  controller.Update(At(50), Duration{101ms}, true);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  controller.Update(At(100), Duration{101ms}, true);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  controller.Update(At(150), Duration{102ms}, true);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 51200);
  controller.Update(At(200), Duration{102ms}, true);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
  controller.Update(At(300), Duration{102ms}, true);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 40960);
}

void test_ApplicationBacklogDoesNotLowerRateOrPreventProbing() {
  Config config;
  RateController controller{config, At(0)};
  for (std::uint32_t time = 100; time <= 400; time += 100) {
    for (int i = 0; i < 40; ++i) {
      // Only transmitted outcomes enter the controller. Application expirations
      // have their own stream counters and are not evidence of path congestion.
      controller.RecordOutcome(At(time), 100, true, Duration{10ms});
    }
    controller.FeedbackReceived(At(time), Duration{20ms});
    controller.Update(At(time), Duration{200ms});
  }
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 70560);
  TEST_ASSERT_EQUAL_UINT32(0, controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
  auto const allowance = controller.rate_bytes_per_second();
  controller.Update(At(500), Duration{300ms});
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == allowance);
}

void test_SparseHealthyTrafficCanRecoverWithoutClaimingConfidence() {
  Config config;
  config.initial_rate_bytes_per_second = config.min_rate_bytes_per_second;
  RateController controller{config, At(0)};
  for (std::uint32_t time = 100; time <= 5000; time += 100) {
    if (time % 400 == 0) {
      // 2.5 payloads/s cannot fill a 20-sample, 1-second window.
      controller.RecordOutcome(At(time), 800, true, Duration{10ms});
    }
    controller.FeedbackReceived(At(time), Duration{20ms});
    controller.Update(At(time), Duration{90ms});
    TEST_ASSERT_FALSE(controller.metrics().window_valid);
  }
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() > 4000);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() <= 20000);
  TEST_ASSERT_TRUE(controller.metrics().window_samples > 0);
  TEST_ASSERT_FALSE(controller.metrics().history_limited);
}

void test_SparseProbingNeedsFreshFinalOutcomeAndNoLoss() {
  Config config;
  config.initial_rate_bytes_per_second = config.min_rate_bytes_per_second;
  RateController controller{config, At(0)};
  controller.RecordOutcome(At(400), 800, false, Duration::zero());
  controller.RecordOutcome(At(800), 800, true, Duration{10ms});
  controller.RecordOutcome(At(1200), 800, true, Duration{10ms});
  controller.FeedbackReceived(At(1200), Duration{20ms});
  controller.Update(At(1200), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 2000);
  controller.RecordOutcome(At(1600), 800, true, Duration{10ms});
  controller.FeedbackReceived(At(1600), Duration{20ms});
  controller.Update(At(1600), Duration::zero());
  auto const after_probe = controller.rate_bytes_per_second();
  TEST_ASSERT_TRUE(after_probe > 2000);
  // Fresh feedback alone cannot repeatedly recycle the same successful sample.
  for (std::uint32_t time = 1700; time <= 3000; time += 100) {
    controller.FeedbackReceived(At(time), Duration{20ms});
    controller.Update(At(time), Duration::zero());
  }
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == after_probe);
}

void test_StaleFeedbackIncludesStartupAndReducesOncePerEpisode() {
  Config config;
  RateController controller{config, At(0)};
  controller.Update(At(499), Duration::zero());
  TEST_ASSERT_FALSE(controller.metrics().feedback_stale);
  controller.Update(At(500), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().feedback_stale);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  // Control was run at 499 ms: protection may execute at the next interval.
  controller.Update(At(600), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 32000);
  controller.Update(At(1000), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 32000);
  controller.FeedbackReceived(At(1100), Duration{20ms});
  controller.Update(At(1100), Duration::zero());
  TEST_ASSERT_FALSE(controller.metrics().feedback_stale);
  controller.Update(At(1600), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 16000);
}

void test_BoundedHistoryAdvertisesUncertaintyAndForbidsProbing() {
  Config config;
  config.max_history_packets = 2;
  config.min_samples = 1;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 3);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 2);
  TEST_ASSERT_TRUE(controller.metrics().history_limited);
  TEST_ASSERT_FALSE(controller.metrics().window_valid);
  RecordBatch(controller, 200, 3);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  controller.Update(At(1200), Duration::zero());
  TEST_ASSERT_FALSE(controller.metrics().history_limited);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 0);
}

void test_UnconfirmedOutcomesPreventProbingUntilWindowBoundary() {
  Config config;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 40);
  controller.RecordUnconfirmed(At(150));
  RecordBatch(controller, 200, 40);
  RecordBatch(controller, 1000, 40);
  TEST_ASSERT_TRUE(controller.metrics().feedback_incomplete);
  TEST_ASSERT_TRUE(controller.metrics().constraints_unmet);
  TEST_ASSERT_FALSE(controller.metrics().window_valid);
  TEST_ASSERT_EQUAL_UINT32(0, controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 64000);
  controller.Update(At(1149), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().feedback_incomplete);
  controller.Update(At(1150), Duration::zero());
  TEST_ASSERT_FALSE(controller.metrics().feedback_incomplete);
  TEST_ASSERT_TRUE(controller.metrics().window_valid);
  TEST_ASSERT_FALSE(controller.metrics().constraints_unmet);
  RecordBatch(controller, 1300, 40);
  RecordBatch(controller, 1400, 40);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 67200);
  controller.RecordUnconfirmed(At(1450));
  controller.ResetPath(At(1500));
  TEST_ASSERT_FALSE(controller.metrics().feedback_incomplete);
}

void test_ReorderedReportsKeepTemporalWindowSorted() {
  Config config;
  config.min_samples = 1;
  RateController controller{config, At(0)};
  controller.RecordOutcome(At(10), 100, false, Duration{10ms});
  controller.RecordOutcome(At(9), 100, true, Duration{10ms});
  controller.FeedbackReceived(At(1009), Duration{20ms});
  controller.Update(At(1009), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 1);
  TEST_ASSERT_EQUAL_UINT32(1000, controller.metrics().window_loss_per_mille);
}

void test_ZeroLossTargetAndExactFractionComparison() {
  Config config;
  config.max_loss_per_mille = 0;
  RateController zero_loss_controller{config, At(0)};
  RecordBatch(zero_loss_controller, 100, 40);
  RecordBatch(zero_loss_controller, 200, 40);
  TEST_ASSERT_TRUE(zero_loss_controller.rate_bytes_per_second() == 67200);
  RecordBatch(zero_loss_controller, 300, 0, 1);
  TEST_ASSERT_TRUE(zero_loss_controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(zero_loss_controller.rate_bytes_per_second() == 53760);

  config.max_loss_per_mille = 47;
  RateController fractional_controller{config, At(0)};
  RecordBatch(fractional_controller, 100, 20, 1);
  TEST_ASSERT_EQUAL_UINT32(
      48, fractional_controller.metrics().window_loss_per_mille);
  TEST_ASSERT_TRUE(fractional_controller.metrics().constraints_unmet);
  TEST_ASSERT_TRUE(fractional_controller.rate_bytes_per_second() == 51200);
}

void test_RateBoundsAndResetPath() {
  Config config;
  config.min_samples = 1;
  config.min_rate_bytes_per_second = 90;
  config.initial_rate_bytes_per_second = 100;
  config.max_rate_bytes_per_second = 101;
  RateController controller{config, At(0)};
  RecordBatch(controller, 100, 1);
  RecordBatch(controller, 200, 1);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 101);
  RecordBatch(controller, 300, 1);
  RecordBatch(controller, 400, 1);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 101);
  RecordBatch(controller, 500, 0, 20);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 90);
  RecordBatch(controller, 600, 0, 20);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 90);
  controller.ResetPath(At(700));
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 100);
  TEST_ASSERT_TRUE(controller.metrics().delivered_bytes == 0);
  TEST_ASSERT_TRUE(controller.metrics().window_samples == 0);
  TEST_ASSERT_FALSE(controller.metrics().constraints_unmet);
  TEST_ASSERT_FALSE(controller.metrics().feedback_stale);
}

void test_TimeAndByteCounterOverflowEdges() {
  Config config;
  RateController controller{config, TimePoint::min()};
  controller.Update(TimePoint::max(), Duration::zero());
  TEST_ASSERT_TRUE(controller.metrics().feedback_stale);
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 32000);
  // A backwards clock reading must not create a second elapsed control period.
  controller.Update(TimePoint::min(), Duration::zero());
  TEST_ASSERT_TRUE(controller.rate_bytes_per_second() == 32000);

  RateController bytes_controller{config, At(0)};
  auto const huge_payload = std::numeric_limits<std::size_t>::max();
  bytes_controller.RecordOutcome(At(1), huge_payload, true, Duration{10ms});
  bytes_controller.RecordOutcome(At(2), huge_payload, true, Duration{10ms});
  bytes_controller.Update(At(100), Duration::zero());
  if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
    TEST_ASSERT_TRUE(bytes_controller.metrics().delivered_bytes ==
                     std::numeric_limits<std::uint64_t>::max());
    TEST_ASSERT_TRUE(bytes_controller.metrics().window_delivered_bytes ==
                     std::numeric_limits<std::uint64_t>::max());
  } else {
    TEST_ASSERT_TRUE(bytes_controller.metrics().delivered_bytes ==
                     static_cast<std::uint64_t>(huge_payload) * 2);
  }
}

}  // namespace ae::test_rate_controller

int test_rate_controller() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_rate_controller::test_ValidateConfiguration);
  RUN_TEST(ae::test_rate_controller::
               test_EmptyWindowAndFeedbackAreNotDeliveryEvidence);
  RUN_TEST(ae::test_rate_controller::
               test_HealthyProbingRequiresFreshSamplesAndHysteresis);
  RUN_TEST(ae::test_rate_controller::test_LossTargetChangesRateDecision);
  RUN_TEST(
      ae::test_rate_controller::test_OldLossSamplesDoNotRepeatedlyCollapseRate);
  RUN_TEST(ae::test_rate_controller::test_TemporalWindowExpiresAtExactBoundary);
  RUN_TEST(
      ae::test_rate_controller::test_SuccessesDoNotReapplyOldLossReduction);
  RUN_TEST(
      ae::test_rate_controller::test_WindowLengthChangesHistoryAndDecision);
  RUN_TEST(
      ae::test_rate_controller::test_LatencyTargetProtectsBeforeFinalOutcome);
  RUN_TEST(
      ae::test_rate_controller::test_QueueGrowthIsRateLimitedByControlInterval);
  RUN_TEST(ae::test_rate_controller::
               test_ApplicationBacklogDoesNotLowerRateOrPreventProbing);
  RUN_TEST(ae::test_rate_controller::
               test_SparseHealthyTrafficCanRecoverWithoutClaimingConfidence);
  RUN_TEST(ae::test_rate_controller::
               test_SparseProbingNeedsFreshFinalOutcomeAndNoLoss);
  RUN_TEST(ae::test_rate_controller::
               test_StaleFeedbackIncludesStartupAndReducesOncePerEpisode);
  RUN_TEST(ae::test_rate_controller::
               test_BoundedHistoryAdvertisesUncertaintyAndForbidsProbing);
  RUN_TEST(ae::test_rate_controller::
               test_UnconfirmedOutcomesPreventProbingUntilWindowBoundary);
  RUN_TEST(
      ae::test_rate_controller::test_ReorderedReportsKeepTemporalWindowSorted);
  RUN_TEST(
      ae::test_rate_controller::test_ZeroLossTargetAndExactFractionComparison);
  RUN_TEST(ae::test_rate_controller::test_RateBoundsAndResetPath);
  RUN_TEST(ae::test_rate_controller::test_TimeAndByteCounterOverflowEdges);
  return UNITY_END();
}

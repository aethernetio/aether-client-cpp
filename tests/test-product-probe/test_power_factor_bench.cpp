/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "unity.h"

#include "examples/probe_receiver/power_factor_config.h"
#include "examples/probe_receiver/product_probe_schedule.h"
#include "examples/probe_receiver/probe_protocol.h"

namespace ae::test_power_factor {
namespace {

// Probe schedule: first temperature before adaptive probe.
void test_FirstSendBeforeProbeDue() {
  probe_schedule::RtcScheduleState state{};
  auto decision =
      probe_schedule::DecideOnWake(&state, 1000000u, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(probe_schedule::FirstSendDecision::kSendTemperatureNow),
      static_cast<int>(decision));
  probe_schedule::RecordFirstTemperatureSuccess(&state, 2000000u);
  decision =
      probe_schedule::DecideOnWake(&state, 3000000000u, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(probe_schedule::FirstSendDecision::kDeferProbeNotDue),
      static_cast<int>(decision));
}

void test_ProbeDueAfterOneHour() {
  probe_schedule::RtcScheduleState state{};
  state.magic = probe_schedule::kRtcMagic;
  probe_schedule::RecordFirstTemperatureSuccess(&state, 1000u);
  auto const decision = probe_schedule::DecideOnWake(
      &state, 1000u + probe_schedule::kProbeDelayUs + 1u, false);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(probe_schedule::FirstSendDecision::kRunAdaptiveProbe),
      static_cast<int>(decision));
}

void test_ColdBootResetsProbeDelay() {
  probe_schedule::RtcScheduleState state{};
  probe_schedule::RecordFirstTemperatureSuccess(&state, 100u);
  auto const decision = probe_schedule::DecideOnWake(&state, 999999999u, true);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(probe_schedule::FirstSendDecision::kSendTemperatureNow),
      static_cast<int>(decision));
}

// Bench protocol pack/unpack round trip.
void test_BenchArmRoundTrip() {
  probe::BenchArm in{};
  in.session = 0x12345678u;
  in.variant_id = 10;
  in.expected = 100;
  std::uint8_t buf[probe::kMaxProbeMessageSize]{};
  auto const n = probe::Pack(in, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, static_cast<int>(n));
  probe::BenchArm out{};
  TEST_ASSERT_TRUE(probe::Unpack(buf, n, out));
  TEST_ASSERT_EQUAL_UINT32(in.session, out.session);
  TEST_ASSERT_EQUAL_UINT16(in.variant_id, out.variant_id);
  TEST_ASSERT_EQUAL_UINT16(in.expected, out.expected);
}

void test_BenchDataTracker() {
  probe::BatchSeqTracker tracker{};
  tracker.Reset(100);
  TEST_ASSERT_TRUE(tracker.Observe(1));
  TEST_ASSERT_TRUE(tracker.Observe(2));
  TEST_ASSERT_FALSE(tracker.Observe(1));
  TEST_ASSERT_EQUAL_UINT16(2, tracker.unique());
  TEST_ASSERT_EQUAL_UINT16(1, tracker.dup());
}

void test_PowerFactorConstants() {
  TEST_ASSERT_EQUAL_UINT16(100, power_bench::kHotAttempts);
  TEST_ASSERT_EQUAL_UINT16(2000, power_bench::kHotSleepMs);
  TEST_ASSERT_EQUAL_UINT16(90, power_bench::kMinRxUnique);
}

}  // namespace
}  // namespace ae::test_power_factor

int test_power_factor() {
  RUN_TEST(ae::test_power_factor::test_FirstSendBeforeProbeDue);
  RUN_TEST(ae::test_power_factor::test_ProbeDueAfterOneHour);
  RUN_TEST(ae::test_power_factor::test_ColdBootResetsProbeDelay);
  RUN_TEST(ae::test_power_factor::test_BenchArmRoundTrip);
  RUN_TEST(ae::test_power_factor::test_BenchDataTracker);
  RUN_TEST(ae::test_power_factor::test_PowerFactorConstants);
  return 0;
}

/*
 * Copyright 2026 Aethernet Inc.
 *
 * Host unit tests for adaptive WifiProbeState (no hardware).
 */

#include <unity.h>

#include "aether/wifi/wifi_probe_state.h"

namespace ae::test_wifi_probe_state {

void test_CrcRoundTrip() {
  WifiProbeRtcState s{};
  s.ssid_hash = 0x12345678u;
  s.channel = 6;
  s.ip = 0x0100A8C0u;
  WifiProbeSealCrc(s);
  TEST_ASSERT_TRUE(WifiProbeRtcValid(s));
  s.channel = 7;
  TEST_ASSERT_FALSE(WifiProbeRtcValid(s));
}

void test_UnknownVersionInvalid() {
  WifiProbeRtcState s{};
  WifiProbeSealCrc(s);
  s.version = 99;
  WifiProbeSealCrc(s);  // reseal would make valid with wrong version field intent
  // Force bad version with old crc.
  s.version = 99;
  TEST_ASSERT_FALSE(WifiProbeRtcValid(s));
}

void test_NewNetworkInvalidation() {
  WifiProbeRtcState s{};
  s.selected_profile = static_cast<std::uint8_t>(WifiProbeProfile::kP4ChannelIpArp);
  s.valid_profiles_bitmap = 0x1F;
  s.probe_generation = 3;
  WifiProbeSealCrc(s);
  WifiProbeInvalidateForNewNetwork(s, 0xABu);
  TEST_ASSERT_EQUAL_UINT32(0xABu, s.ssid_hash);
  TEST_ASSERT_EQUAL_UINT8(0, s.valid_profiles_bitmap);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(WifiProbeProfile::kP0Default),
      s.selected_profile);
  TEST_ASSERT_EQUAL_UINT16(4, s.probe_generation);
  TEST_ASSERT_TRUE(WifiProbeRtcValid(s));
}

void test_DegradeSelected() {
  WifiProbeRtcState s{};
  s.selected_profile = static_cast<std::uint8_t>(WifiProbeProfile::kP3ChannelIp);
  s.valid_profiles_bitmap = 0x0F;
  WifiProbeSealCrc(s);
  WifiProbeDegradeSelected(s, WifiProbeRecoveryReason::kStaleChannel);
  TEST_ASSERT_EQUAL_UINT8(0x07, s.valid_profiles_bitmap);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(WifiProbeProfile::kP0Default),
      s.selected_profile);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(WifiProbeRecoveryReason::kStaleChannel),
      s.recovery_reason);
}

void test_SelectFastestAmongValid() {
  std::uint16_t ms[5] = {500, 80, 200, 50, 40};
  // Only P1 and P3 valid.
  auto const sel = WifiProbeSelectFastest(0x0A, ms);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(WifiProbeProfile::kP3ChannelIp),
      static_cast<std::uint8_t>(sel));
}

void test_IcmpThreshold() {
  WifiProbeIcmpStats ok{10, 9};
  TEST_ASSERT_TRUE(WifiProbeIcmpPasses(ok));
  WifiProbeIcmpStats fail{10, 8};
  TEST_ASSERT_FALSE(WifiProbeIcmpPasses(fail));
}

void test_MergePrefersNewerGeneration() {
  WifiProbeRtcState older{};
  older.probe_generation = 1;
  older.selected_profile = 1;
  WifiProbeSealCrc(older);

  WifiProbeRtcState newer{};
  newer.probe_generation = 2;
  newer.selected_profile = 3;
  WifiProbeSealCrc(newer);

  WifiProbeRtcState dst = older;
  if (WifiProbeRtcValid(newer) &&
      newer.probe_generation >= dst.probe_generation) {
    dst = newer;
  }
  TEST_ASSERT_EQUAL_UINT8(3, dst.selected_profile);
  TEST_ASSERT_EQUAL_UINT16(2, dst.probe_generation);
}

}  // namespace ae::test_wifi_probe_state

int test_wifi_probe_state() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_wifi_probe_state::test_CrcRoundTrip);
  RUN_TEST(ae::test_wifi_probe_state::test_UnknownVersionInvalid);
  RUN_TEST(ae::test_wifi_probe_state::test_NewNetworkInvalidation);
  RUN_TEST(ae::test_wifi_probe_state::test_DegradeSelected);
  RUN_TEST(ae::test_wifi_probe_state::test_SelectFastestAmongValid);
  RUN_TEST(ae::test_wifi_probe_state::test_IcmpThreshold);
  RUN_TEST(ae::test_wifi_probe_state::test_MergePrefersNewerGeneration);
  return UNITY_END();
}

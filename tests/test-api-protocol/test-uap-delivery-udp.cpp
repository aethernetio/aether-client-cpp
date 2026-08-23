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

#include <cstdint>

#include "aether/access_points/filter_endpoints.h"
#include "aether/config.h"
#include "aether/types/address.h"

#include "examples/benches/aether_uap_delivery_timing_bench/common/udp_proof_types.h"

namespace ae::test_uap_delivery_udp {
namespace {

Endpoint MakeEndpoint(Protocol protocol) {
  Endpoint endpoint{};
  endpoint.protocol = protocol;
  endpoint.port = 9000;
  endpoint.address = IpV4Addr{{127, 0, 0, 1}};
  return endpoint;
}

}  // namespace

void test_UdpBuildConfigMacros() {
  TEST_ASSERT_EQUAL_INT(1, AE_SUPPORT_UDP);
#if !AE_SUPPORT_TCP
  TEST_ASSERT_EQUAL_INT(0, AE_SUPPORT_TCP);
#else
  TEST_ASSERT_EQUAL_INT(1, AE_SUPPORT_TCP);
#endif
}

void test_FilterProtocolRejectsTcpAcceptsUdp() {
  auto const tcp = MakeEndpoint(Protocol::kTcp);
  auto const udp = MakeEndpoint(Protocol::kUdp);

  TEST_ASSERT_TRUE((FilterProtocol<Protocol::kTcp, Protocol::kUdp>(udp)));
#if !AE_SUPPORT_TCP
  TEST_ASSERT_FALSE((FilterProtocol<Protocol::kTcp, Protocol::kUdp>(tcp)));
#else
  TEST_ASSERT_TRUE((FilterProtocol<Protocol::kTcp, Protocol::kUdp>(tcp)));
#endif
}

void test_ProtocolUdpEnumValue() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Protocol::kTcp), 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Protocol::kUdp), 1);
}

void test_BenchRefusesTcpSample() {
  using ae::bench::uap::BenchProtocol;
  using ae::bench::uap::IsMeasuredProtocolOk;
  using ae::bench::uap::RefuseTcpSample;
#if AE_SUPPORT_TCP
  // TCP+UDP USER_CONFIG: TCP samples are allowed (registration needs TCP).
  TEST_ASSERT_FALSE(RefuseTcpSample(BenchProtocol::kTcp, BenchProtocol::kUdp));
  TEST_ASSERT_FALSE(RefuseTcpSample(BenchProtocol::kUdp, BenchProtocol::kTcp));
  TEST_ASSERT_TRUE(IsMeasuredProtocolOk(BenchProtocol::kTcp));
  TEST_ASSERT_TRUE(IsMeasuredProtocolOk(BenchProtocol::kUdp));
#else
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kTcp, BenchProtocol::kUdp));
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kUdp, BenchProtocol::kTcp));
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kTcp, BenchProtocol::kTcp));
  TEST_ASSERT_FALSE(RefuseTcpSample(BenchProtocol::kUdp, BenchProtocol::kUdp));
  TEST_ASSERT_FALSE(
      RefuseTcpSample(BenchProtocol::kUnknown, BenchProtocol::kUdp));
  TEST_ASSERT_FALSE(IsMeasuredProtocolOk(BenchProtocol::kTcp));
  TEST_ASSERT_TRUE(IsMeasuredProtocolOk(BenchProtocol::kUdp));
#endif
}

}  // namespace ae::test_uap_delivery_udp

int test_uap_delivery_udp() {
  UNITY_BEGIN();
  using namespace ae::test_uap_delivery_udp;  // NOLINT
  RUN_TEST(test_UdpBuildConfigMacros);
  RUN_TEST(test_FilterProtocolRejectsTcpAcceptsUdp);
  RUN_TEST(test_ProtocolUdpEnumValue);
  RUN_TEST(test_BenchRefusesTcpSample);
  return UNITY_END();
}

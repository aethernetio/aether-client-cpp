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

#include "examples/benches/aether_uap_delivery_timing_bench/common/bench_types.h"

namespace ae::test_uap_delivery_udp {
namespace {

Endpoint MakeEndpoint(Protocol protocol) {
  Endpoint endpoint{};
  endpoint.protocol = protocol;
  endpoint.port = 9000;
  endpoint.address = IpV4Addr{{127, 0, 0, 1}};
  return endpoint;
}

// Mirrors examples/.../udp_proof.h RefuseTcpSample without pulling Client.
bool RefuseTcpSample(ae::bench::uap::BenchProtocol own,
                     ae::bench::uap::BenchProtocol destination) {
  return own == ae::bench::uap::BenchProtocol::kTcp ||
         destination == ae::bench::uap::BenchProtocol::kTcp;
}

}  // namespace

void test_UdpBuildConfigMacros() {
  TEST_ASSERT_EQUAL_INT(1, AE_SUPPORT_UDP);
#if !AE_SUPPORT_TCP
  // Expected for config/user_config_uap_delivery_udp.h builds.
  TEST_ASSERT_EQUAL_INT(0, AE_SUPPORT_TCP);
#else
  TEST_IGNORE_MESSAGE(
      "AE_SUPPORT_TCP=1: TCP reject is asserted in the UDP USER_CONFIG build");
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
  TEST_IGNORE_MESSAGE(
      "AE_SUPPORT_TCP=1 in this build; TCP reject asserted only in UDP "
      "USER_CONFIG tree");
#endif
}

void test_ProtocolUdpEnumValue() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Protocol::kTcp), 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Protocol::kUdp), 1);
}

void test_BenchRefusesTcpSample() {
  using ae::bench::uap::BenchProtocol;
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kTcp, BenchProtocol::kUdp));
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kUdp, BenchProtocol::kTcp));
  TEST_ASSERT_TRUE(RefuseTcpSample(BenchProtocol::kTcp, BenchProtocol::kTcp));
  TEST_ASSERT_FALSE(RefuseTcpSample(BenchProtocol::kUdp, BenchProtocol::kUdp));
  TEST_ASSERT_FALSE(
      RefuseTcpSample(BenchProtocol::kUnknown, BenchProtocol::kUdp));
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

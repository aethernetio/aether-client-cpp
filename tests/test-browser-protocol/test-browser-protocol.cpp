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

/**
 * Native Protocol / AddrVersion / Endpoint wire compatibility (no Emscripten).
 */

#include <cstdint>
#include <string>
#include <vector>

#include <unity.h>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/transport/browser/browser_endpoint.h"
#include "aether/types/address.h"

namespace ae::test_browser_protocol {
namespace {

Endpoint RoundTripEndpoint(Endpoint const& original) {
  std::vector<std::uint8_t> buffer;
  {
    auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{buffer}};
    TEST_ASSERT(archive.Save(original));
  }

  Endpoint loaded{};
  {
    auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{buffer}};
    TEST_ASSERT(archive.Load(loaded));
  }
  return loaded;
}

void AssertClassicEndpointEqual(Endpoint const& left, Endpoint const& right) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(left.protocol),
                          static_cast<std::uint8_t>(right.protocol));
  TEST_ASSERT_EQUAL_UINT16(left.port, right.port);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(left.address.Index()),
                          static_cast<std::uint8_t>(right.address.Index()));
  if (left.address.Index() == AddrVersion::kIpV4) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(left.address.Get<IpV4Addr>().ipv4_value,
                                  right.address.Get<IpV4Addr>().ipv4_value, 4);
  }
}

void AssertBrowserEndpointEqual(Endpoint const& left, Endpoint const& right) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(left.protocol),
                          static_cast<std::uint8_t>(right.protocol));
  TEST_ASSERT_EQUAL_UINT16(left.port, right.port);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AddrVersion::kBrowser),
                          static_cast<std::uint8_t>(right.address.Index()));
  auto const& la = left.address.Get<BrowserAddr>();
  auto const& ra = right.address.Get<BrowserAddr>();
  TEST_ASSERT_EQUAL_UINT8(la.representation_version, ra.representation_version);
  TEST_ASSERT_EQUAL_STRING(la.hostname.c_str(), ra.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING(la.path.c_str(), ra.path.c_str());
  TEST_ASSERT_EQUAL_STRING(la.gateway_target.c_str(), ra.gateway_target.c_str());
}

}  // namespace

void test_ProtocolEnumValues() {
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<std::uint8_t>(Protocol::kTcp));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<std::uint8_t>(Protocol::kUdp));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<std::uint8_t>(Protocol::kWebSocket));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<std::uint8_t>(Protocol::kHttp));
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<std::uint8_t>(Protocol::kHttps));
  TEST_ASSERT_EQUAL_UINT8(5,
                          static_cast<std::uint8_t>(Protocol::kWebSocketSecure));
}

void test_AddrVersionBrowserIsFour() {
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<std::uint8_t>(AddrVersion::kBrowser));
}

void test_ClassicTcpEndpointRoundTrip() {
  Endpoint original{{IpV4Addr{{192, 0, 2, 10}}, 9010}, Protocol::kTcp};
  auto const loaded = RoundTripEndpoint(original);
  AssertClassicEndpointEqual(original, loaded);
}

void test_ClassicUdpEndpointRoundTrip() {
  Endpoint original{{IpV4Addr{{198, 51, 100, 1}}, 4242}, Protocol::kUdp};
  auto const loaded = RoundTripEndpoint(original);
  AssertClassicEndpointEqual(original, loaded);
}

void test_BrowserHttpsEndpointRoundTrip() {
  BrowserAddr addr{};
  addr.representation_version = 1;
  addr.hostname = "gateway.example";
  addr.path = "/aether/v1";
  addr.gateway_target = "cloud-a";

  Endpoint original{{Address{addr}, 443}, Protocol::kHttps};
  auto const loaded = RoundTripEndpoint(original);
  AssertBrowserEndpointEqual(original, loaded);
}

void test_BrowserWebSocketSecureEndpointRoundTrip() {
  BrowserAddr addr{};
  addr.representation_version = 1;
  addr.hostname = "ws.example.org";
  addr.path = "/aether/v1/ws";
  addr.gateway_target = "edge-42";

  Endpoint original{{Address{addr}, 8443}, Protocol::kWebSocketSecure};
  auto const loaded = RoundTripEndpoint(original);
  AssertBrowserEndpointEqual(original, loaded);
}

void test_BuildWebSocketSecureUrlPreservesFields() {
  BrowserAddr addr{};
  addr.hostname = "ws.example.org";
  addr.path = "/aether/v1/ws";
  addr.gateway_target = "edge-42";
  Endpoint endpoint{{Address{addr}, 8443}, Protocol::kWebSocketSecure};

  auto const url = BuildWebSocketUrl(endpoint);
  TEST_ASSERT_EQUAL_STRING(
      "wss://ws.example.org:8443/aether/v1/ws?target=edge-42", url.c_str());
}

void test_BuildHttpsConnectUrl() {
  BrowserAddr addr{};
  addr.hostname = "gateway.example";
  addr.path = "/aether/v1";
  addr.gateway_target = "cloud-a";
  Endpoint endpoint{{Address{addr}, 443}, Protocol::kHttps};

  auto const url = BuildHttpConnectUrl(endpoint);
  TEST_ASSERT_EQUAL_STRING("https://gateway.example/aether/v1/connect",
                           url.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"target\":\"cloud-a\"}",
                           BuildConnectTargetJson(endpoint).c_str());
}

}  // namespace ae::test_browser_protocol

int test_browser_protocol() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_browser_protocol::test_ProtocolEnumValues);
  RUN_TEST(ae::test_browser_protocol::test_AddrVersionBrowserIsFour);
  RUN_TEST(ae::test_browser_protocol::test_ClassicTcpEndpointRoundTrip);
  RUN_TEST(ae::test_browser_protocol::test_ClassicUdpEndpointRoundTrip);
  RUN_TEST(ae::test_browser_protocol::test_BrowserHttpsEndpointRoundTrip);
  RUN_TEST(
      ae::test_browser_protocol::test_BrowserWebSocketSecureEndpointRoundTrip);
  RUN_TEST(ae::test_browser_protocol::test_BuildWebSocketSecureUrlPreservesFields);
  RUN_TEST(ae::test_browser_protocol::test_BuildHttpsConnectUrl);
  return UNITY_END();
}

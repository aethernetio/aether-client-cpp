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

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether/access_points/filter_endpoints.h"
#include "aether/config.h"
#include "aether/types/address.h"

#include "stream_matrix/bench_persistence.h"

#if defined(AETHER_STREAM_MATRIX_UDP_BUILD_CONFIG_TEST) && \
    AETHER_STREAM_MATRIX_UDP_BUILD_CONFIG_TEST
#if AE_SUPPORT_TCP != 0
#error UDP-only build must set AE_SUPPORT_TCP=0
#endif
#if AE_SUPPORT_UDP == 0
#error UDP-only build must set AE_SUPPORT_UDP=1
#endif
#endif

namespace {

int g_failures = 0;

void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

void test_domain_state_probe() {
  auto missing = std::filesystem::temp_directory_path() /
                 "stream_matrix_missing_state_dir";
  std::error_code ec;
  std::filesystem::remove_all(missing, ec);
  Expect(!ae::stream_matrix_bench::DomainStateHasPersistedObjects(missing),
         "empty/missing dir not registered");
}

void test_bootstrap_marker_roundtrip() {
  ae::stream_matrix_bench::BootstrapMarker in{
      .parent_uid = "3ac93165-3d37-4970-87a6-fa4ee27744e4",
      .sender_client_id = "sender",
      .receiver_client_id = "receiver",
      .sender_uid = "a",
      .receiver_uid = "b",
  };
  auto path = std::filesystem::temp_directory_path() / "sm_bootstrap_marker.txt";
  ae::stream_matrix_bench::WriteBootstrapMarker(path, in);
  auto out = ae::stream_matrix_bench::ReadBootstrapMarker(path);
  Expect(out.has_value(), "marker read");
  Expect(out->sender_uid == "a" && out->receiver_uid == "b",
         "marker fields");
  std::filesystem::remove(path);
}

void test_udp_endpoint_filter_when_tcp_disabled() {
#if AE_SUPPORT_TCP == 0 && AE_SUPPORT_UDP != 0
  ae::Endpoint tcp_ep;
  tcp_ep.address = ae::Address{ae::IpV4Addr{{127, 0, 0, 1}}};
  tcp_ep.port = 443;
  tcp_ep.protocol = ae::Protocol::kTcp;
  ae::Endpoint udp_ep;
  udp_ep.address = ae::Address{ae::IpV4Addr{{127, 0, 0, 1}}};
  udp_ep.port = 443;
  udp_ep.protocol = ae::Protocol::kUdp;
  Expect(!ae::FilterProtocol<ae::Protocol::kTcp>(tcp_ep),
         "TCP filtered when AE_SUPPORT_TCP=0");
  Expect(ae::FilterProtocol<ae::Protocol::kUdp>(udp_ep),
         "UDP allowed when AE_SUPPORT_UDP=1");
#else
  std::cout << "skip endpoint filter test (not UDP-only TU)\n";
#endif
}

void test_udp_only_compile_flag_when_enabled() {
#if defined(AETHER_STREAM_MATRIX_UDP_BUILD_CONFIG_TEST) && \
    AETHER_STREAM_MATRIX_UDP_BUILD_CONFIG_TEST
  Expect(AE_SUPPORT_TCP == 0, "AE_SUPPORT_TCP=0 in udp build");
  Expect(AE_SUPPORT_UDP != 0, "AE_SUPPORT_UDP=1 in udp build");
#endif
}

}  // namespace

int main() {
  test_domain_state_probe();
  test_bootstrap_marker_roundtrip();
  test_udp_endpoint_filter_when_tcp_disabled();
  test_udp_only_compile_flag_when_enabled();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_registration_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_registration_test: OK\n";
  return 0;
}

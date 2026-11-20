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

#include <algorithm>
#include <array>
#include <cstddef>

#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/transport/system_sockets/udp/udp.h"
#include "tests/test-safe-stream/stream-test-ctx.h"

namespace ae::test_receipt_pages {
void test_IndependentPagesRecoverMixedReceiptsAcrossDroppedAndReorderedAcks() {
  constexpr std::size_t kPackets = 2400;
  constexpr std::size_t kMtu = 256;
  TestContext context;
  EmulatedDatagramNetwork network{12345};
  EmulatedDatagramProfile path;
  path.delay = 25ms;
  path.mtu = kMtu;
  path.bytes_per_second = 200000000;
  path.burst_every = 2;
  path.burst_length = 1;
  network.SetProfile(0, path);
  path.burst_every = 7;
  path.reorder_per_mille = 450;
  path.reorder_delay = 25ms;
  network.SetProfile(1, path);

  using Transport = UdpTransport<EmulatedDatagramSocket, 16>;
  Transport tx{context, AddressPort{}, EmulatedDatagramSocket{network, 0}};
  Transport rx{context, AddressPort{}, EmulatedDatagramSocket{network, 1}};
  adaptive_datagram::Config config;
  config.max_in_flight_packets = 8192;
  config.max_datagram_size = kMtu;
  config.initial_rate_bytes_per_second = config.max_rate_bytes_per_second;
  auto clock = [&] { return network.now(); };
  AdaptiveDatagramStream sender{context, config, clock};
  ++config.session_id;
  config.max_in_flight_packets = 512;
  AdaptiveDatagramStream receiver{context, config, clock};
  sender.LinkOut(tx);
  receiver.LinkOut(rx);

  std::array<bool, kPackets> outcomes{};
  bool unique = true;
  std::size_t feedback_count{}, max_feedback_size{}, received{}, peak_flight{};
  MultiSubscription subs;
  subs += tx.out_data_event().Subscribe([&](auto const& wire) {
    ++feedback_count;
    max_feedback_size = std::max(max_feedback_size, wire.size());
  });
  subs += receiver.out_data_event().Subscribe([&](auto const&) { ++received; });
  subs += sender.delivery_event().Subscribe([&](auto const& outcome) {
    if (!outcome.sequence || outcome.sequence > outcomes.size()) {
      unique = false;
      return;
    }
    auto& seen = outcomes[outcome.sequence - 1];
    unique &= !seen;
    seen = true;
  });
  auto advance = [&](Duration by) {
    network.AdvanceTo(network.now() + by);
    for (int i = 0; i < 4; ++i) {
      context.Update(network.now());
    }
  };
  for (std::size_t i = 0; i < kPackets; ++i) {
    sender.Write(DataBuffer{42});
    advance(20us);
    peak_flight = std::max(peak_flight, sender.statistics().in_flight);
  }
  for (int i = 0; i < 800; ++i) {
    advance(1ms);
  }
  auto const stats = sender.statistics();
  TEST_ASSERT_TRUE(peak_flight > 512);
  TEST_ASSERT_EQUAL(kPackets, stats.sent);
  TEST_ASSERT_TRUE(network.stats(1).dropped_burst > 0);
  TEST_ASSERT_TRUE(network.stats(1).reordered > 0);
  TEST_ASSERT_TRUE(feedback_count > 1 && max_feedback_size <= kMtu);
  TEST_ASSERT_EQUAL(kPackets / 2, received);
  TEST_ASSERT_EQUAL(received, stats.delivered);
  TEST_ASSERT_EQUAL(kPackets - received, stats.lost);
  TEST_ASSERT_EQUAL(0, stats.unconfirmed);
  TEST_ASSERT_TRUE(unique && std::all_of(outcomes.begin(), outcomes.end(),
                                         [](bool seen) { return seen; }));
  TEST_ASSERT_EQUAL(0, stats.malformed + receiver.statistics().malformed);
  TEST_ASSERT_EQUAL(0, context.sched.overflow_counter());
}
}  // namespace ae::test_receipt_pages

int test_receipt_pages() {
  using namespace ae::test_receipt_pages;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(
      test_IndependentPagesRecoverMixedReceiptsAcrossDroppedAndReorderedAcks);
  return UNITY_END();
}

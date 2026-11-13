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
#include <cstdint>
#include <vector>

#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/client_messages/p2p_adaptive_datagram_stream.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/transport/system_sockets/udp/udp.h"
#include "tests/test-safe-stream/stream-test-ctx.h"

namespace ae::test_adaptive_stream {
using Config = adaptive_datagram::Config;
using Transport = UdpTransport<EmulatedDatagramSocket, 16>;

struct Pair {
  explicit Pair(Config config = {})
      : network{12345},
        first_udp{context, AddressPort{}, EmulatedDatagramSocket{network, 0}},
        second_udp{context, AddressPort{}, EmulatedDatagramSocket{network, 1}},
        first{context, config, [this] { return network.now(); }},
        second{context, PeerConfig(config), [this] { return network.now(); }} {
    first.LinkOut(first_udp);
    second.LinkOut(second_udp);
    subscriptions += second.out_data_event().Subscribe(
        [this](auto const& data) { received.push_back(data); });
  }
  static Config PeerConfig(Config config) {
    config.session_id += 1;
    return config;
  }
  void Step() {
    network.AdvanceTo(network.now() + 1ms);
    for (int i = 0; i < 4; ++i) {
      context.Update(network.now());
    }
  }
  void Run(unsigned milliseconds, bool feed = false, unsigned every_ms = 10) {
    for (unsigned i = 0; i < milliseconds; ++i) {
      if (feed && i % every_ms == 0) {
        first.Write(DataBuffer(800, static_cast<std::uint8_t>(i)));
      }
      Step();
    }
  }
  TestContext context;
  EmulatedDatagramNetwork network;
  Transport first_udp;
  Transport second_udp;
  AdaptiveDatagramStream first;
  AdaptiveDatagramStream second;
  MultiSubscription subscriptions;
  std::vector<DataBuffer> received;
};

void test_ProductionUdpTransportWithEmulatedSocketDeliversAndAcknowledges() {
  Pair pair;
  int status = -1;
  auto& write = pair.first.Write(DataBuffer{1, 2, 3, 4});
  Subscription subscription = write.status_event().Subscribe(
      [&](auto s) { status = static_cast<int>(s); });
  pair.Run(700);
  TEST_ASSERT_EQUAL(0, status);
  TEST_ASSERT_EQUAL(1, pair.received.size());
  TEST_ASSERT_EQUAL(4, pair.received.front().size());
  TEST_ASSERT_EQUAL(1, pair.first.statistics().delivered);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().lost);
  TEST_ASSERT_TRUE(pair.first.statistics().feedback_sent == 0);
  TEST_ASSERT_TRUE(pair.second.statistics().feedback_sent > 1);
}

void test_LocalSuccessDoesNotHideNetworkLoss() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.loss_per_mille = 1000;
  pair.network.SetProfile(0, profile);
  int local_successes = 0;
  MultiSubscription subscriptions;
  for (int i = 0; i < 8; ++i) {
    auto& write = pair.first.Write(DataBuffer(100, 7));
    subscriptions += write.status_event().Subscribe([&](auto s) {
      if (s == WriteAction::Status::kSuccess) {
        ++local_successes;
      }
    });
  }
  pair.Run(800);
  TEST_ASSERT_TRUE(pair.received.empty());
  TEST_ASSERT_EQUAL(8, local_successes);
  TEST_ASSERT_EQUAL(8, pair.first.statistics().unconfirmed);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().lost);
  TEST_ASSERT_TRUE(pair.first.metrics().feedback_stale);
  TEST_ASSERT_TRUE(pair.first.rate_bytes_per_second() < 64000);
}

void test_DuplicatesAndReorderingDoNotDuplicateMessages() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.duplicate_per_mille = 1000;
  profile.reorder_per_mille = 500;
  profile.jitter = 8ms;
  profile.reorder_delay = 25ms;
  pair.network.SetProfile(0, profile);
  for (int i = 0; i < 30; ++i) {
    pair.first.Write(DataBuffer{static_cast<std::uint8_t>(i)});
    pair.Run(3);
  }
  pair.Run(800);
  TEST_ASSERT_EQUAL(30, pair.received.size());
  std::vector<int> counts(30);
  for (auto const& data : pair.received) {
    ++counts[data.front()];
  }
  TEST_ASSERT_TRUE(
      std::all_of(counts.begin(), counts.end(), [](auto n) { return n == 1; }));
  TEST_ASSERT_TRUE(pair.second.statistics().duplicates > 0);
  TEST_ASSERT_EQUAL(30, pair.first.statistics().delivered);
  TEST_ASSERT_TRUE(pair.network.stats(0).reordered > 0);
}

void test_LateDataIsNotDeliveredToApplication() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.delay = 150ms;
  pair.network.SetProfile(0, profile);
  pair.first.Write(DataBuffer(100, 9));
  pair.Run(800);
  TEST_ASSERT_TRUE(pair.received.empty());
  TEST_ASSERT_EQUAL(1, pair.first.statistics().late);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().delivered);
  // Repeated receipts of this one old packet are not new congestion samples.
  TEST_ASSERT_TRUE(pair.first.rate_bytes_per_second() == 51200);
  TEST_ASSERT_TRUE(pair.first.metrics().rtt >= 150ms);
  TEST_ASSERT_TRUE(pair.first.metrics().rtt < 200ms);
}

void test_LostFeedbackIsRepeatedWithoutDuplicatingPayload() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.blackhole = true;
  pair.network.SetProfile(1, profile);
  pair.first.Write(DataBuffer(100, 10));
  pair.Run(150);
  profile.blackhole = false;
  pair.network.SetProfile(1, profile);
  pair.Run(650);
  TEST_ASSERT_EQUAL(1, pair.received.size());
  TEST_ASSERT_EQUAL(1, pair.first.statistics().delivered);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().lost);
}

void test_BoundedQueueExpiresBlockedWrites() {
  Config config;
  config.max_queue_packets = 4;
  Pair pair{config};
  EmulatedDatagramProfile profile;
  profile.blocked = true;
  pair.network.SetProfile(0, profile);
  for (int i = 0; i < 10; ++i) {
    pair.first.Write(DataBuffer(100, 11));
  }
  TEST_ASSERT_EQUAL(4, pair.first.statistics().queued);
  TEST_ASSERT_EQUAL(6, pair.first.statistics().rejected);
  pair.Run(800);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().queued);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().in_flight);
  TEST_ASSERT_TRUE(pair.received.empty());
  TEST_ASSERT_EQUAL(4, pair.first.statistics().expired);
  profile.blocked = false;
  pair.network.SetProfile(0, profile);
  pair.Run(20);
  pair.first.Write(DataBuffer(100, 12));
  pair.Run(800);
  TEST_ASSERT_EQUAL(1, pair.received.size());
}

void test_CancelAndUnlinkStopPendingWrites() {
  Pair pair;
  int status = -1;
  auto& write = pair.first.Write(DataBuffer(100, 13));
  Subscription subscription = write.status_event().Subscribe(
      [&](auto s) { status = static_cast<int>(s); });
  write.Stop();
  pair.Run(20);
  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kStop), status);
  TEST_ASSERT_TRUE(pair.received.empty());
  auto& next = pair.first.Write(DataBuffer(100, 14));
  int stopped = 0;
  Subscription other = next.status_event().Subscribe([&](auto s) {
    if (s == WriteAction::Status::kStop) {
      ++stopped;
    }
  });
  pair.first.Unlink();
  TEST_ASSERT_EQUAL(1, stopped);
  pair.Run(20);
  TEST_ASSERT_TRUE(pair.received.empty());
}

void test_RateRespondsToCapacityDropAndRecovery() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.bytes_per_second = 120000;
  profile.queue_bytes = 4000;
  pair.network.SetProfile(0, profile);
  pair.Run(5000, true, 8);
  auto const before = pair.first.rate_bytes_per_second();
  profile.bytes_per_second = 18000;
  pair.network.SetProfile(0, profile);
  pair.Run(5000, true, 8);
  auto const constrained = pair.first.rate_bytes_per_second();
  TEST_ASSERT_TRUE(constrained < before);
  TEST_ASSERT_TRUE(pair.first.statistics().lost + pair.first.statistics().late >
                   0);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_queue > 0);
  profile.bytes_per_second = 120000;
  pair.network.SetProfile(0, profile);
  pair.Run(5000, true, 8);
  auto const recovery_start = pair.received.size();
  pair.Run(3000, true, 8);
  TEST_ASSERT_TRUE(pair.first.rate_bytes_per_second() > constrained);
  // A recovered 120kB/s path must regain useful capacity, not merely add a few
  // percent to the collapsed allowance. Producer demand is 100kB/s here.
  TEST_ASSERT_TRUE((pair.received.size() - recovery_start) * 800 / 3 >= 70000);
  TEST_ASSERT_TRUE(pair.first.statistics().queued <= 128);
  TEST_ASSERT_TRUE(pair.first.statistics().in_flight <= 512);
  TEST_ASSERT_EQUAL(0, pair.context.sched.overflow_counter());
}

void test_RejectsOversizeAndMalformedWireData() {
  Pair pair;
  pair.first.Write(DataBuffer(2000, 1));
  TEST_ASSERT_EQUAL(1, pair.first.statistics().rejected);
  pair.first_udp.Write(DataBuffer{255});
  pair.Run(40);
  TEST_ASSERT_EQUAL(1, pair.second.statistics().malformed);
  TEST_ASSERT_TRUE(pair.received.empty());
}

void test_SeededRandomLossAndJitterHaveExactOutcomeAccounting() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.loss_per_mille = 150;
  profile.jitter = 8ms;
  profile.reorder_per_mille = 100;
  profile.reorder_delay = 20ms;
  pair.network.SetProfile(0, profile);
  profile.loss_per_mille = 300;
  pair.network.SetProfile(1, profile);
  for (int i = 0; i < 100; ++i) {
    pair.first.Write(DataBuffer(100, static_cast<std::uint8_t>(i)));
    pair.Run(8);
  }
  pair.Run(900);
  auto const stats = pair.first.statistics();
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_random > 0);
  TEST_ASSERT_TRUE(pair.network.stats(1).dropped_random > 0);
  TEST_ASSERT_EQUAL(100, stats.sent);
  TEST_ASSERT_EQUAL(stats.sent, stats.delivered + stats.lost + stats.late +
                                    stats.unconfirmed);
  TEST_ASSERT_EQUAL(pair.received.size(), stats.delivered);
  TEST_ASSERT_EQUAL(0, stats.in_flight);
}

void test_DeliveryCallbackCanUnlink() {
  Pair pair;
  int outcomes = 0;
  Subscription subscription =
      pair.first.delivery_event().Subscribe([&](auto const&) {
        ++outcomes;
        pair.first.Unlink();
      });
  pair.first.Write(DataBuffer(100, 20));
  pair.Run(800);
  TEST_ASSERT_EQUAL(1, outcomes);
  TEST_ASSERT_FALSE(pair.first.stream_info().is_writable);
}

void test_BidirectionalLowRateDoesNotStarvePayload() {
  Config config;
  config.initial_rate_bytes_per_second = 3000;
  Pair pair{config};
  int reverse_received = 0;
  Subscription subscription = pair.first.out_data_event().Subscribe(
      [&](auto const&) { ++reverse_received; });
  for (int i = 0; i < 12; ++i) {
    pair.first.Write(DataBuffer(50, 21));
    pair.second.Write(DataBuffer(50, 22));
    pair.Run(100);
  }
  pair.Run(800);
  TEST_ASSERT_TRUE(pair.received.size() >= 8);
  TEST_ASSERT_TRUE(reverse_received >= 8);
}

void test_RejectedActionCanBeDestroyedBeforeSchedulerRuns() {
  TestContext context;
  {
    Config config;
    config.max_loss_per_mille = 1001;
    AdaptiveDatagramStream stream{context, config};
    TEST_ASSERT_FALSE(stream.valid());
    stream.Write(DataBuffer(1, 23));
  }
  context.Update();
  TEST_ASSERT_EQUAL(0, context.sched.overflow_counter());
}

void test_RejectedCallbackMayDestroyItsStream() {
  TestContext context;
  auto stream = std::make_unique<AdaptiveDatagramStream>(context, Config{});
  auto& rejected = stream->Write(DataBuffer(1, 24));
  Subscription subscription =
      rejected.status_event().Subscribe([&](auto) { stream.reset(); });
  context.Update();
  TEST_ASSERT_TRUE(stream == nullptr);
  context.Update();
}

void test_DelaySpikeDoesNotPreventRecoveryProbes() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.delay = 150ms;
  pair.network.SetProfile(0, profile);
  pair.first.Write(DataBuffer(100, 25));
  pair.Run(250);
  TEST_ASSERT_TRUE(pair.first.metrics().latest_latency > 100ms);
  profile.delay = 10ms;
  pair.network.SetProfile(0, profile);
  pair.first.Write(DataBuffer(100, 26));
  pair.Run(800);
  TEST_ASSERT_EQUAL(1, pair.received.size());
  TEST_ASSERT_EQUAL(26, pair.received.front().front());
}

void test_CancelledMessageIdsDoNotEvictWireReceipts() {
  Pair pair;
  EmulatedDatagramProfile profile;
  profile.blackhole = true;
  pair.network.SetProfile(1, profile);
  pair.first.Write(DataBuffer(100, 27));
  pair.Run(40);
  for (int batch = 0; batch < 10; ++batch) {
    for (int i = 0; i < 100; ++i) {
      pair.first.Write(DataBuffer(1, 0)).Stop();
    }
    pair.Run(2);
  }
  pair.first.Write(DataBuffer(100, 28));
  pair.Run(40);
  profile.blackhole = false;
  pair.network.SetProfile(1, profile);
  pair.Run(800);
  TEST_ASSERT_EQUAL(2, pair.received.size());
  TEST_ASSERT_EQUAL(2, pair.first.statistics().delivered);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().lost);
  TEST_ASSERT_EQUAL(0, pair.first.statistics().unconfirmed);
}

void test_DefaultClockUsesSchedulerEpoch() {
  TestContext context;
  EmulatedDatagramNetwork network;
  Transport udp{context, AddressPort{}, EmulatedDatagramSocket{network, 0}};
  AdaptiveDatagramStream stream{context, Config{}};
  stream.LinkOut(udp);
  auto const before = Now();
  auto const next = context.Update(before);
  TEST_ASSERT_TRUE(next > before);
}

void test_DeliveryCallbackMayDestroyItsStream() {
  Pair pair;
  Config config;
  config.session_id = 7;
  auto stream = std::make_unique<AdaptiveDatagramStream>(
      pair.context, config, [&] { return pair.network.now(); });
  stream->LinkOut(pair.first_udp);
  int outcomes = 0;
  Subscription subscription =
      stream->delivery_event().Subscribe([&](auto const&) {
        ++outcomes;
        stream.reset();
      });
  stream->Write(DataBuffer(100, 29));
  pair.Run(800);
  TEST_ASSERT_EQUAL(1, outcomes);
  TEST_ASSERT_TRUE(stream == nullptr);
}
void test_P2pAdaptiveRequiresAnActualLinkedUdpEndpoint() {
  P2pStream::SendPathTransportView path;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.aggregated.link_state = LinkState::kLinked;
  path.aggregated.is_reliable = false;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.linked_udp_endpoint = true;
  TEST_ASSERT_TRUE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.aggregated.link_state = LinkState::kUnlinked;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.aggregated.link_state = LinkState::kLinkError;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
}

void test_P2pAdaptiveRejectsTcpAndMixedRouteFailover() {
  P2pStream::SendPathTransportView path;
  path.aggregated.link_state = LinkState::kLinked;
  path.aggregated.is_reliable = true;
  path.linked_tcp_endpoint = true;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  // Mixed paths report is_reliable=false, so that flag alone is insufficient.
  path.aggregated.is_reliable = false;
  path.linked_udp_endpoint = true;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.linked_tcp_endpoint = false;
  TEST_ASSERT_TRUE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
  path.aggregated.is_reliable = true;
  TEST_ASSERT_FALSE(P2pAdaptiveDatagramStream::IsSupportedSendPath(path));
}
}  // namespace ae::test_adaptive_stream

int test_adaptive_stream() {
  using namespace ae::test_adaptive_stream;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(
      test_ProductionUdpTransportWithEmulatedSocketDeliversAndAcknowledges);
  RUN_TEST(test_P2pAdaptiveRequiresAnActualLinkedUdpEndpoint);
  RUN_TEST(test_P2pAdaptiveRejectsTcpAndMixedRouteFailover);
  RUN_TEST(test_LocalSuccessDoesNotHideNetworkLoss);
  RUN_TEST(test_DuplicatesAndReorderingDoNotDuplicateMessages);
  RUN_TEST(test_LateDataIsNotDeliveredToApplication);
  RUN_TEST(test_LostFeedbackIsRepeatedWithoutDuplicatingPayload);
  RUN_TEST(test_BoundedQueueExpiresBlockedWrites);
  RUN_TEST(test_CancelAndUnlinkStopPendingWrites);
  RUN_TEST(test_RateRespondsToCapacityDropAndRecovery);
  RUN_TEST(test_RejectsOversizeAndMalformedWireData);
  RUN_TEST(test_SeededRandomLossAndJitterHaveExactOutcomeAccounting);
  RUN_TEST(test_DeliveryCallbackCanUnlink);
  RUN_TEST(test_BidirectionalLowRateDoesNotStarvePayload);
  RUN_TEST(test_RejectedActionCanBeDestroyedBeforeSchedulerRuns);
  RUN_TEST(test_RejectedCallbackMayDestroyItsStream);
  RUN_TEST(test_DelaySpikeDoesNotPreventRecoveryProbes);
  RUN_TEST(test_CancelledMessageIdsDoNotEvictWireReceipts);
  RUN_TEST(test_DefaultClockUsesSchedulerEpoch);
  RUN_TEST(test_DeliveryCallbackMayDestroyItsStream);
  return UNITY_END();
}

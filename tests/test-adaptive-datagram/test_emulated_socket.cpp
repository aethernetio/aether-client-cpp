/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/types/span.h"

namespace ae::test_emulated_socket {

struct Arrival {
  std::uint8_t tag;
  TimePoint at;
  bool operator==(Arrival const&) const = default;
};

void Connect(EmulatedDatagramSocket& socket) {
  socket.Connect(AddressPort{}, {});
}

void Send(EmulatedDatagramSocket& socket, std::uint8_t tag,
          std::size_t size = 1) {
  auto data = DataBuffer(size, tag);
  auto sent = socket.Send(Span{data.data(), data.size()});
  TEST_ASSERT_TRUE(sent.has_value());
  TEST_ASSERT_EQUAL(size, *sent);
}

struct RecordingPair {
  explicit RecordingPair(std::uint64_t seed = 1)
      : network{seed}, left{network, 0}, right{network, 1} {
    Connect(left);
    Connect(right);
    left.RecvData([this](Span<std::uint8_t> data) {
      at_left.push_back({data.data()[0], network.now()});
    });
    right.RecvData([this](Span<std::uint8_t> data) {
      at_right.push_back({data.data()[0], network.now()});
    });
  }

  EmulatedDatagramNetwork network;
  EmulatedDatagramSocket left;
  EmulatedDatagramSocket right;
  std::vector<Arrival> at_left;
  std::vector<Arrival> at_right;
};

void test_ConnectMtuAndDisconnectedSend() {
  EmulatedDatagramNetwork network;
  EmulatedDatagramSocket socket{network, 0};
  std::uint8_t value = 7;
  TEST_ASSERT_FALSE(socket.Send(Span{&value, 1}).has_value());

  auto profile = EmulatedDatagramProfile{};
  profile.mtu = 4;
  network.SetProfile(0, profile);
  bool connected = false;
  socket.Connect(AddressPort{}, [&](ISocket::ConnectionState state) {
    connected = state == ISocket::ConnectionState::kConnected;
  });
  TEST_ASSERT_TRUE(connected);

  DataBuffer oversized(5, 1);
  TEST_ASSERT_FALSE(
      socket.Send(Span{oversized.data(), oversized.size()}).has_value());
  TEST_ASSERT_TRUE(network.stats(0).sent == 0);
  Send(socket, 7, 4);
  TEST_ASSERT_TRUE(network.stats(0).sent == 1);
  socket.Disconnect();
  TEST_ASSERT_FALSE(socket.Send(Span{&value, 1}).has_value());
}

void test_BandwidthQueueLimitAndRelease() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.bytes_per_second = 1000;
  profile.queue_bytes = 150;
  pair.network.SetProfile(0, profile);

  Send(pair.left, 1, 100);
  Send(pair.left, 2, 100);  // UDP accepted; the network tail-drop is separate.
  TEST_ASSERT_TRUE(pair.network.stats(0).sent == 2);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_queue == 1);
  TEST_ASSERT_EQUAL(100, pair.network.stats(0).peak_queue_bytes);
  pair.network.AdvanceTo(TimePoint{} + 109ms);
  TEST_ASSERT_TRUE(pair.at_right.empty());
  pair.network.AdvanceTo(TimePoint{} + 110ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  TEST_ASSERT_EQUAL(1, pair.at_right.front().tag);
  TEST_ASSERT_TRUE(pair.at_right.front().at == TimePoint{} + 110ms);

  Send(pair.left, 3, 100);
  pair.network.AdvanceTo(TimePoint{} + 219ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  pair.network.AdvanceTo(TimePoint{} + 220ms);
  TEST_ASSERT_EQUAL(2, pair.at_right.size());
  TEST_ASSERT_EQUAL(3, pair.at_right.back().tag);
  TEST_ASSERT_TRUE(pair.network.stats(0).bytes_sent == 300);
}

void test_DuplicatesConsumeSerializationAndQueueCapacity() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.bytes_per_second = 1000;
  profile.queue_bytes = 200;
  profile.duplicate_per_mille = 1000;
  pair.network.SetProfile(0, profile);
  Send(pair.left, 9, 100);
  Send(pair.left, 8, 100);
  TEST_ASSERT_TRUE(pair.network.stats(0).duplicates == 1);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_queue == 1);
  TEST_ASSERT_EQUAL(200, pair.network.stats(0).peak_queue_bytes);

  pair.network.AdvanceTo(TimePoint{} + 110ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  pair.network.AdvanceTo(TimePoint{} + 209ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  pair.network.AdvanceTo(TimePoint{} + 210ms);
  TEST_ASSERT_EQUAL(2, pair.at_right.size());
  TEST_ASSERT_EQUAL(9, pair.at_right[0].tag);
  TEST_ASSERT_EQUAL(9, pair.at_right[1].tag);
}

void test_AllLossAndBlackholeStillSucceedLocally() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.loss_per_mille = 1000;
  pair.network.SetProfile(0, profile);
  for (std::uint8_t i = 0; i < 16; ++i) {
    Send(pair.left, i);
  }
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_TRUE(pair.at_right.empty());
  TEST_ASSERT_TRUE(pair.network.stats(0).sent == 16);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_random == 16);

  profile.loss_per_mille = 0;
  profile.blackhole = true;
  pair.network.SetProfile(0, profile);
  for (std::uint8_t i = 0; i < 5; ++i) {
    Send(pair.left, i);
  }
  pair.network.AdvanceTo(TimePoint{} + 200ms);
  TEST_ASSERT_TRUE(pair.at_right.empty());
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_blackhole == 5);
}

void test_PeriodicBurstDropsOnlyConfiguredPositions() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.burst_every = 5;
  profile.burst_length = 2;
  pair.network.SetProfile(0, profile);
  for (std::uint8_t i = 0; i < 10; ++i) {
    Send(pair.left, i);
  }
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_burst == 4);
  TEST_ASSERT_EQUAL(6, pair.at_right.size());
  std::uint8_t const expected[] = {2, 3, 4, 7, 8, 9};
  for (std::size_t i = 0; i < pair.at_right.size(); ++i) {
    TEST_ASSERT_EQUAL(expected[i], pair.at_right[i].tag);
  }
}

void test_ForwardAndReverseProfilesAreIndependent() {
  RecordingPair pair;
  auto forward = EmulatedDatagramProfile{};
  forward.loss_per_mille = 1000;
  pair.network.SetProfile(0, forward);
  auto reverse = EmulatedDatagramProfile{};
  reverse.delay = 1ms;
  pair.network.SetProfile(1, reverse);
  for (std::uint8_t i = 0; i < 5; ++i) {
    Send(pair.left, i);
    Send(pair.right, static_cast<std::uint8_t>(100 + i));
  }
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_TRUE(pair.at_right.empty());
  TEST_ASSERT_EQUAL(5, pair.at_left.size());
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_random == 5);
  TEST_ASSERT_TRUE(pair.network.stats(1).dropped_random == 0);
  TEST_ASSERT_TRUE(pair.network.stats(1).delivered == 5);
}

struct RecordedRun {
  std::vector<Arrival> arrivals;
  EmulatedDatagramStats stats;
};

RecordedRun RunSeededTraffic(std::uint64_t seed, bool reverse_traffic) {
  RecordingPair pair{seed};
  auto profile = EmulatedDatagramProfile{};
  profile.delay = 15ms;
  profile.jitter = 10ms;
  profile.loss_per_mille = 200;
  profile.reorder_per_mille = 300;
  profile.reorder_delay = 40ms;
  profile.duplicate_per_mille = 100;
  pair.network.SetProfile(0, profile);
  pair.network.SetProfile(1, profile);
  for (std::uint8_t i = 0; i < 100; ++i) {
    Send(pair.left, i);
    if (reverse_traffic) {
      Send(pair.right, i);
    }
  }
  pair.network.AdvanceTo(TimePoint{} + 500ms);
  return {std::move(pair.at_right), pair.network.stats(0)};
}

void test_SeedReproducesLossJitterReorderAndDuplication() {
  auto first = RunSeededTraffic(12345, false);
  auto repeated = RunSeededTraffic(12345, false);
  auto bidirectional = RunSeededTraffic(12345, true);
  auto other_seed = RunSeededTraffic(54321, false);
  TEST_ASSERT_TRUE(first.arrivals == repeated.arrivals);
  TEST_ASSERT_TRUE(first.arrivals == bidirectional.arrivals);
  TEST_ASSERT_FALSE(first.arrivals == other_seed.arrivals);
  TEST_ASSERT_TRUE(first.stats.dropped_random > 0);
  TEST_ASSERT_TRUE(first.stats.reordered > 0);
  TEST_ASSERT_TRUE(first.stats.duplicates > 0);
  TEST_ASSERT_TRUE(first.stats.dropped_random == repeated.stats.dropped_random);
  TEST_ASSERT_TRUE(first.stats.duplicates == repeated.stats.duplicates);
  TEST_ASSERT_TRUE(first.stats.reordered == repeated.stats.reordered);
}

void test_ReorderAndProfileChangePreserveScheduledArrival() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.reorder_per_mille = 1000;
  profile.reorder_delay = 40ms;
  pair.network.SetProfile(0, profile);
  Send(pair.left, 1);
  profile.reorder_per_mille = 0;
  pair.network.SetProfile(0, profile);
  Send(pair.left, 2);
  pair.network.AdvanceTo(TimePoint{} + 20ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  TEST_ASSERT_EQUAL(2, pair.at_right[0].tag);
  pair.network.AdvanceTo(TimePoint{} + 60ms);
  TEST_ASSERT_EQUAL(2, pair.at_right.size());
  TEST_ASSERT_EQUAL(1, pair.at_right[1].tag);
  TEST_ASSERT_TRUE(pair.network.stats(0).reordered == 1);
}

void test_DisconnectAndReconnectRejectOldGeneration() {
  RecordingPair pair;
  Send(pair.left, 1);
  pair.right.Disconnect();
  Connect(pair.right);
  Send(pair.left, 2);
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  TEST_ASSERT_EQUAL(2, pair.at_right[0].tag);
  TEST_ASSERT_TRUE(pair.network.stats(0).dropped_disconnected == 1);
}

void test_DestroyedReceiverCannotDeliverIntoReplacement() {
  EmulatedDatagramNetwork network;
  EmulatedDatagramSocket left{network, 0};
  Connect(left);
  auto right = std::make_unique<EmulatedDatagramSocket>(network, 1);
  Connect(*right);
  int calls = 0;
  right->RecvData([&calls](Span<std::uint8_t>) { ++calls; });
  Send(left, 1);
  right.reset();
  right = std::make_unique<EmulatedDatagramSocket>(network, 1);
  Connect(*right);
  right->RecvData([&calls](Span<std::uint8_t>) { ++calls; });
  Send(left, 2);
  network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_EQUAL(1, calls);
  TEST_ASSERT_TRUE(network.stats(0).dropped_disconnected == 1);
}

void test_ReceiveCallbackMayDestroyItsSocket() {
  EmulatedDatagramNetwork network;
  EmulatedDatagramSocket left{network, 0};
  Connect(left);
  auto right = std::make_unique<EmulatedDatagramSocket>(network, 1);
  Connect(*right);
  int calls = 0;
  struct CallbackContext {
    int* calls;
    std::unique_ptr<EmulatedDatagramSocket>* right;
  } context{&calls, &right};
  right->RecvData([&context](Span<std::uint8_t>) {
    ++*context.calls;
    context.right->reset();
  });
  Send(left, 1);
  Send(left, 2);
  network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_NULL(right.get());
  TEST_ASSERT_EQUAL(1, calls);
  TEST_ASSERT_TRUE(network.stats(0).dropped_disconnected == 1);
}

void test_MoveConstructionAndAssignmentPreserveRegistration() {
  RecordingPair pair;
  EmulatedDatagramNetwork spare_network;
  EmulatedDatagramSocket spare{spare_network, 0};
  Connect(spare);
  Send(pair.left, 1);
  auto moved = EmulatedDatagramSocket{std::move(pair.right)};
  spare = std::move(moved);
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  Send(spare, 2);
  pair.network.AdvanceTo(TimePoint{} + 200ms);
  TEST_ASSERT_EQUAL(1, pair.at_left.size());
  TEST_ASSERT_EQUAL(2, pair.at_left[0].tag);
  std::uint8_t value = 0;
  TEST_ASSERT_FALSE(moved.Send(Span{&value, 1}).has_value());
  TEST_ASSERT_FALSE(pair.right.Send(Span{&value, 1}).has_value());
  // The displaced registration was removed from the old network.
  EmulatedDatagramSocket replacement{spare_network, 0};
  Connect(replacement);
}

void test_UnblockNotifiesOnceAndCallbackCanSend() {
  RecordingPair pair;
  auto profile = EmulatedDatagramProfile{};
  profile.blocked = true;
  pair.network.SetProfile(0, profile);
  std::uint8_t value = 7;
  auto sent = pair.left.Send(Span{&value, 1});
  TEST_ASSERT_TRUE(sent.has_value());
  TEST_ASSERT_EQUAL(0, *sent);
  TEST_ASSERT_TRUE(pair.network.stats(0).sent == 0);
  int calls = 0;
  struct CallbackContext {
    EmulatedDatagramSocket* socket;
    int* calls;
  } context{&pair.left, &calls};
  pair.left.ReadyToWrite([&context]() {
    ++*context.calls;
    Send(*context.socket, 7);
  });
  profile.blocked = false;
  pair.network.SetProfile(0, profile);
  TEST_ASSERT_EQUAL(0, calls);
  pair.network.AdvanceTo(pair.network.now());
  TEST_ASSERT_EQUAL(1, calls);
  pair.network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_EQUAL(1, calls);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
}

void test_DestroyedBlockedWriterHasNoReadyCallback() {
  EmulatedDatagramNetwork network;
  auto left = std::make_unique<EmulatedDatagramSocket>(network, 0);
  Connect(*left);
  auto profile = EmulatedDatagramProfile{};
  profile.blocked = true;
  network.SetProfile(0, profile);
  int calls = 0;
  left->ReadyToWrite([&calls]() { ++calls; });
  std::uint8_t value = 1;
  TEST_ASSERT_TRUE(left->Send(Span{&value, 1}) == 0);
  left.reset();
  profile.blocked = false;
  network.SetProfile(0, profile);
  network.AdvanceTo(TimePoint{} + 100ms);
  TEST_ASSERT_EQUAL(0, calls);
}

void test_AdvanceTimeIsExplicitAndEqualTimeIsAllowed() {
  RecordingPair pair;
  auto const initial = pair.network.now();
  TEST_ASSERT_TRUE(initial == TimePoint{});
  Send(pair.left, 1);
  TEST_ASSERT_TRUE(pair.network.now() == initial);
  TEST_ASSERT_TRUE(pair.at_right.empty());
  pair.network.AdvanceTo(initial);
  TEST_ASSERT_TRUE(pair.network.now() == initial);
  pair.network.AdvanceTo(initial + 100ms);
  TEST_ASSERT_TRUE(pair.network.now() == initial + 100ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  pair.network.AdvanceTo(initial + 100ms);
  TEST_ASSERT_EQUAL(1, pair.at_right.size());
  // Backwards time is an assertion contract, so it requires a separate process
  // death test rather than aborting the entire Unity suite here.
}

}  // namespace ae::test_emulated_socket

int test_emulated_socket() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_emulated_socket::test_ConnectMtuAndDisconnectedSend);
  RUN_TEST(ae::test_emulated_socket::test_BandwidthQueueLimitAndRelease);
  RUN_TEST(ae::test_emulated_socket::
               test_DuplicatesConsumeSerializationAndQueueCapacity);
  RUN_TEST(
      ae::test_emulated_socket::test_AllLossAndBlackholeStillSucceedLocally);
  RUN_TEST(
      ae::test_emulated_socket::test_PeriodicBurstDropsOnlyConfiguredPositions);
  RUN_TEST(
      ae::test_emulated_socket::test_ForwardAndReverseProfilesAreIndependent);
  RUN_TEST(ae::test_emulated_socket::
               test_SeedReproducesLossJitterReorderAndDuplication);
  RUN_TEST(ae::test_emulated_socket::
               test_ReorderAndProfileChangePreserveScheduledArrival);
  RUN_TEST(
      ae::test_emulated_socket::test_DisconnectAndReconnectRejectOldGeneration);
  RUN_TEST(ae::test_emulated_socket::
               test_DestroyedReceiverCannotDeliverIntoReplacement);
  RUN_TEST(ae::test_emulated_socket::test_ReceiveCallbackMayDestroyItsSocket);
  RUN_TEST(ae::test_emulated_socket::
               test_MoveConstructionAndAssignmentPreserveRegistration);
  RUN_TEST(
      ae::test_emulated_socket::test_UnblockNotifiesOnceAndCallbackCanSend);
  RUN_TEST(
      ae::test_emulated_socket::test_DestroyedBlockedWriterHasNoReadyCallback);
  RUN_TEST(ae::test_emulated_socket::
               test_AdvanceTimeIsExplicitAndEqualTimeIsAllowed);
  return UNITY_END();
}

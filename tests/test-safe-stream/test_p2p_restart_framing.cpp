#include <unity.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>
#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/safe_stream/details/safe_stream_api.h"
#include "aether/stream_api/api_call_adapter.h"
#include "aether/stream_api/sized_packet_gate.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/transport/system_sockets/udp/udp.h"
#include "tests/test-safe-stream/stream-test-ctx.h"
#include "tests/test-stream/mock_write_stream.h"

namespace ae::test_p2p_restart_framing {
class WireClient final : public SafeStreamApi {
 public:
  void Ack(SafeStreamOffset value) override {
    ClientMethod<&SafeStreamApi::Ack>(value);
  }
  void RequestRepeat(SafeStreamOffset value) override {
    ClientMethod<&SafeStreamApi::RequestRepeat>(value);
  }
  void SendReset(SafeStreamOffset begin, SafeStreamWireDelta delta,
                 std::uint8_t repeats, DataBuffer data) override {
    ClientMethod<&SafeStreamApi::SendReset>(begin, delta, repeats,
                                            std::move(data));
  }
  void Send(SafeStreamOffset begin, SafeStreamWireDelta delta,
            std::uint8_t repeats, DataBuffer data) override {
    ClientMethod<&SafeStreamApi::Send>(begin, delta, repeats, std::move(data));
  }
};

void RestartDiscardsIncompletePreviousMessage() {
  TestContext context;
  auto now = Now();
  auto raw = std::make_shared<MockWriteStream>(context, 1200);
  MockWriteStream encoder{context, 1200};
  SafeStreamConfig config{1024,
                          512,
                          3,
                          std::chrono::milliseconds{50},
                          std::chrono::milliseconds{0},
                          std::chrono::milliseconds{10}};
  P2pSafeStream receiver{context, config, raw};
  SizedPacketGate framing{context};
  std::vector<DataBuffer> messages;
  MultiSubscription subscriptions;
  subscriptions += encoder.on_write_event().Subscribe(
      [&](DataBuffer& wire) { raw->WriteOut(wire); });
  subscriptions += receiver.out_data_event().Subscribe(
      [&](DataBuffer const& data) { messages.push_back(data); });
  WireClient client;
  auto push = [&](SafeStreamOffset epoch, SafeStreamOffset offset,
                  DataBuffer bytes) {
    auto adapter = ApiCallAdapter{client, encoder};
    adapter->SendReset(epoch, offset, 0, std::move(bytes));
    adapter.Flush();
    context.Update(now += std::chrono::milliseconds{1});
  };
  auto old = framing.WriteIn(DataBuffer(48, 'x'));
  push(100, 0, DataBuffer{old.begin(), old.begin() + 5});
  TEST_ASSERT_EQUAL(0, messages.size());
  // A restarted sender has a new native safe-stream offset. Its first payload
  // starts a new framed message, rather than continuing the old process's one.
  DataBuffer expected{'n', 'e', 'w'};
  push(2000, 0, framing.WriteIn(DataBuffer{expected}));
  TEST_ASSERT_EQUAL(1, messages.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), messages.front().data(),
                           expected.size());
}

void RepeatedResetMarkerKeepsCurrentPartialMessage() {
  TestContext context;
  auto now = Now();
  auto raw = std::make_shared<MockWriteStream>(context, 1200);
  MockWriteStream encoder{context, 1200};
  SafeStreamConfig config{1024,
                          512,
                          3,
                          std::chrono::milliseconds{50},
                          std::chrono::milliseconds{0},
                          std::chrono::milliseconds{10}};
  P2pSafeStream receiver{context, config, raw};
  SizedPacketGate framing{context};
  std::vector<DataBuffer> messages;
  MultiSubscription subscriptions;
  subscriptions += encoder.on_write_event().Subscribe(
      [&](DataBuffer& wire) { raw->WriteOut(wire); });
  subscriptions += receiver.out_data_event().Subscribe(
      [&](DataBuffer const& data) { messages.push_back(data); });
  WireClient client;
  DataBuffer expected(48, 'y');
  auto wire = framing.WriteIn(DataBuffer{expected});
  for (auto [begin, end] :
       {std::pair<std::size_t, std::size_t>{0, 5}, {5, wire.size()}}) {
    auto adapter = ApiCallAdapter{client, encoder};
    adapter->SendReset(100, static_cast<SafeStreamOffset>(begin), 0,
                       DataBuffer{wire.begin() + begin, wire.begin() + end});
    adapter.Flush();
    context.Update(now += std::chrono::milliseconds{1});
  }
  TEST_ASSERT_EQUAL(1, messages.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), messages.front().data(),
                           expected.size());
}

void RestartDiscardsIncompleteOuterFrame() {
  TestContext context;
  auto now = Now();
  auto raw = std::make_shared<MockWriteStream>(context, 1200);
  MockWriteStream encoder{context, 1200};
  SafeStreamConfig config{1024,
                          512,
                          3,
                          std::chrono::milliseconds{50},
                          std::chrono::milliseconds{0},
                          std::chrono::milliseconds{10}};
  P2pSafeStream receiver{context, config, raw};
  SizedPacketGate inner{context};
  SizedPacketGate outer{context};
  std::vector<DataBuffer> messages;
  std::size_t resets = 0;
  MultiSubscription subscriptions;
  subscriptions += encoder.on_write_event().Subscribe(
      [&](DataBuffer& wire) { raw->WriteOut(wire); });
  subscriptions += receiver.receive_reset_event().Subscribe([&]() {
    ++resets;
    outer.Reset();
  });
  subscriptions += receiver.out_data_event().Subscribe(
      [&](DataBuffer const& data) { outer.WriteOut(data); });
  subscriptions += outer.out_data_event().Subscribe(
      [&](DataBuffer const& data) { messages.push_back(data); });
  WireClient client;
  auto push = [&](SafeStreamOffset epoch, SafeStreamOffset offset,
                  DataBuffer bytes) {
    auto adapter = ApiCallAdapter{client, encoder};
    adapter->SendReset(epoch, offset, 0, inner.WriteIn(std::move(bytes)));
    adapter.Flush();
    context.Update(now += std::chrono::milliseconds{1});
  };
  auto old = outer.WriteIn(DataBuffer(48, 'x'));
  push(100, 0, DataBuffer{old.begin(), old.begin() + 5});
  TEST_ASSERT_EQUAL(0, messages.size());
  DataBuffer expected(48, 'y');
  auto wire = outer.WriteIn(DataBuffer{expected});
  auto first = inner.WriteIn(DataBuffer{wire.begin(), wire.begin() + 5});
  auto first_size = first.size();
  // Two complete native messages carry one partial application frame. The
  // second reset marker belongs to the same session and must retain it.
  push(2000, 0, DataBuffer{wire.begin(), wire.begin() + 5});
  push(2000, static_cast<SafeStreamOffset>(first_size),
       DataBuffer{wire.begin() + 5, wire.end()});
  TEST_ASSERT_EQUAL(2, resets);
  TEST_ASSERT_EQUAL(1, messages.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), messages.front().data(),
                           expected.size());
}

void WireDeltaRoundTripsAtFourByteLimit() {
  constexpr auto kShortBoundary = SafeStreamWireDelta::kWireTier0;
  constexpr auto kMediumBoundary = SafeStreamWireDelta::kMaxBoundaryCode;
  std::array<std::uint8_t, SafeStreamWireDelta::kMaxWireBytes> buffer{};

  auto check = [&](std::size_t value, std::size_t expected_size) {
    auto const encoded = SafeStreamWireDelta{value};
    auto const size = encoded.Serialize(buffer.data());
    TEST_ASSERT_EQUAL_UINT(expected_size, size);
    auto const decoded =
        ae::Deserialize<SafeStreamWireDelta>(buffer.data(), size);
    TEST_ASSERT_EQUAL_UINT(size, decoded.bytes_read);
    TEST_ASSERT_EQUAL_UINT(value, decoded.value);
  };
  check(0, SafeStreamWireDelta::kBaseBytes);
  check(kShortBoundary, SafeStreamWireDelta::kBaseBytes);
  check(kShortBoundary + 1, 2);
  check(kMediumBoundary, 2);
  check(kMediumBoundary + 1, 4);
  check(kSafeStreamMaxWindow - 1, 4);
  check(SafeStreamWireDelta::kUpper, 4);
}

void WindowTooLargeForBufferIsRejected() {
  TestContext context;
  SafeStreamConfig config{1024, 1024, 5, 1s, 2ms, 10ms};
  config.buffer_capacity = 2048;
  bool rejected{};
  try {
    P2pSafeStream stream{context, config, nullptr};
  } catch (std::invalid_argument const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
}

void WindowAboveWireLimitIsRejected() {
  TestContext context;
  SafeStreamConfig config{kSafeStreamMaxWindow + 1, 1024, 5, 1s, 2ms, 10ms};
  config.buffer_capacity = 14 * 1024 * 1024;
  bool rejected{};
  try {
    P2pSafeStream stream{context, config, nullptr};
  } catch (std::invalid_argument const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
}

#if AE_SUPPORT_ADAPTIVE_DATAGRAM && defined(SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED)
void AdaptiveTransportRetransmitsAndPreservesFraming(bool large_window) {
  TestContext context;
  EmulatedDatagramNetwork network{12345};
  network.AdvanceTo(Now());
  EmulatedDatagramProfile profile;
  profile.delay = 1ms;
  if (large_window) {
    profile.mtu = 1400;
    auto reverse_profile = profile;
    network.SetProfile(1, reverse_profile);
  }
  profile.burst_every = 1000;
  profile.burst_length = 1;
  network.SetProfile(0, profile);
  using Transport = UdpTransport<EmulatedDatagramSocket, 16>;
  Transport sender_udp{context, AddressPort{},
                       EmulatedDatagramSocket{network, 0}};
  Transport receiver_udp{context, AddressPort{},
                         EmulatedDatagramSocket{network, 1}};

  adaptive_datagram::Config sender_config;
  sender_config.session_id = 11;
  sender_config.peer_session_id = 12;
  sender_config.max_datagram_size = large_window ? 1400 : 256;
  if (large_window) {
    sender_config.initial_rate_bytes_per_second = 2000000;
  }
  sender_config.max_queue_packets = 1;
  sender_config.latency_target = 1s;
  sender_config.loss_window = 3s;
  auto receiver_config = sender_config;
  receiver_config.session_id = sender_config.peer_session_id;
  receiver_config.peer_session_id = sender_config.session_id;
  auto clock = [&network] { return network.now(); };
  auto sender_adaptive =
      std::make_shared<AdaptiveDatagramStream>(context, sender_config, clock);
  auto receiver_adaptive =
      std::make_shared<AdaptiveDatagramStream>(context, receiver_config, clock);
  sender_adaptive->LinkOut(sender_udp);
  receiver_adaptive->LinkOut(receiver_udp);

  SafeStreamConfig config{512, 700, 30, 80ms, 2ms, 10ms, true};
  if (large_window) {
    config.window_size = 256 * 1024;
    config.max_packet_size = 110000;
    config.buffer_capacity = 1024 * 1024;
    config.wait_ack_timeout = 1s;
    config.minimum_ack_timeout = 1s;
  }
  P2pSafeStream sender{context, config, sender_adaptive};
  P2pSafeStream receiver{context, config, receiver_adaptive};
  std::vector<DataBuffer> expected{
      DataBuffer(large_window ? 100000 : 400, 0x31),
      DataBuffer(large_window ? 100000 : 350, 0x52),
      DataBuffer(large_window ? 100000 : 9, 0x73)};
  std::vector<DataBuffer> received;
  MultiSubscription subscriptions;
  subscriptions += receiver.out_data_event().Subscribe(
      [&](DataBuffer const& data) { received.push_back(data); });
  unsigned acknowledged{};
  unsigned failed{};
  for (auto const& data : expected) {
    subscriptions += sender.Write(DataBuffer{data})
                         .status_event()
                         .Subscribe([&](auto status) {
                           if (status == WriteAction::Status::kSuccess) {
                             ++acknowledged;
                           } else {
                             ++failed;
                           }
                         });
  }

  for (unsigned i = 0; i < 4500 && acknowledged < expected.size(); ++i) {
    network.AdvanceTo(network.now() + 1ms);
    context.Update(network.now());
  }

  TEST_ASSERT_TRUE(network.stats(0).dropped_burst > 0);
  TEST_ASSERT_TRUE(sender.stream_info().is_reliable);
  TEST_ASSERT_EQUAL(0, failed);
  TEST_ASSERT_EQUAL(0, sender_adaptive->statistics().rejected);
  TEST_ASSERT_EQUAL(expected.size(), acknowledged);
  TEST_ASSERT_EQUAL(expected.size(), received.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL(expected[i].size(), received[i].size());
    TEST_ASSERT_EQUAL_MEMORY(expected[i].data(), received[i].data(),
                             expected[i].size());
  }
}
void test_AdaptiveTransportRetransmitsAndPreservesFraming() {
  AdaptiveTransportRetransmitsAndPreservesFraming(false);
}
void test_LargeWindowTransfersThreeHundredKilobytes() {
  AdaptiveTransportRetransmitsAndPreservesFraming(true);
}
#endif
}  // namespace ae::test_p2p_restart_framing

int test_p2p_restart_framing() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_p2p_restart_framing::RestartDiscardsIncompletePreviousMessage);
  RUN_TEST(ae::test_p2p_restart_framing::
               RepeatedResetMarkerKeepsCurrentPartialMessage);
  RUN_TEST(ae::test_p2p_restart_framing::RestartDiscardsIncompleteOuterFrame);
  RUN_TEST(ae::test_p2p_restart_framing::WireDeltaRoundTripsAtFourByteLimit);
  RUN_TEST(ae::test_p2p_restart_framing::WindowTooLargeForBufferIsRejected);
  RUN_TEST(ae::test_p2p_restart_framing::WindowAboveWireLimitIsRejected);
#if AE_SUPPORT_ADAPTIVE_DATAGRAM && defined(SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED)
  RUN_TEST(ae::test_p2p_restart_framing::
               test_AdaptiveTransportRetransmitsAndPreservesFraming);
  RUN_TEST(ae::test_p2p_restart_framing::
               test_LargeWindowTransfersThreeHundredKilobytes);
#endif
  return UNITY_END();
}

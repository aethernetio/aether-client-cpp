#include <unity.h>
#include <memory>
#include <vector>
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/safe_stream/details/safe_stream_api.h"
#include "aether/stream_api/api_call_adapter.h"
#include "aether/stream_api/sized_packet_gate.h"
#include "tests/test-safe-stream/stream-test-ctx.h"
#include "tests/test-stream/mock_write_stream.h"

namespace ae::test_p2p_restart_framing {
class WireClient final : public ISafeStreamApi {
 public:
  void Ack(std::uint16_t value) override {
    ClientMethod<&ISafeStreamApi::Ack>(value);
  }
  void RequestRepeat(std::uint16_t value) override {
    ClientMethod<&ISafeStreamApi::RequestRepeat>(value);
  }
  void SendReset(std::uint16_t begin, std::uint16_t delta, std::uint8_t repeats,
                 DataBuffer data) override {
    ClientMethod<&ISafeStreamApi::SendReset>(begin, delta, repeats,
                                             std::move(data));
  }
  void Send(std::uint16_t begin, std::uint16_t delta, std::uint8_t repeats,
            DataBuffer data) override {
    ClientMethod<&ISafeStreamApi::Send>(begin, delta, repeats, std::move(data));
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
  auto push = [&](std::uint16_t epoch, std::uint16_t offset, DataBuffer bytes) {
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
    adapter->SendReset(100, static_cast<std::uint16_t>(begin), 0,
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
  auto push = [&](std::uint16_t epoch, std::uint16_t offset, DataBuffer bytes) {
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
  push(2000, static_cast<std::uint16_t>(first_size),
       DataBuffer{wire.begin() + 5, wire.end()});
  TEST_ASSERT_EQUAL(2, resets);
  TEST_ASSERT_EQUAL(1, messages.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), messages.front().data(),
                           expected.size());
}
}  // namespace ae::test_p2p_restart_framing

int test_p2p_restart_framing() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_p2p_restart_framing::RestartDiscardsIncompletePreviousMessage);
  RUN_TEST(ae::test_p2p_restart_framing::
               RepeatedResetMarkerKeepsCurrentPartialMessage);
  RUN_TEST(ae::test_p2p_restart_framing::RestartDiscardsIncompleteOuterFrame);
  return UNITY_END();
}

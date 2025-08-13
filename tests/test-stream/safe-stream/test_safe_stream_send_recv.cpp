/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/safe_stream_recv_action.h"
#include "aether/stream_api/safe_stream/safe_stream_send_action.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_safe_stream_send_recv {
constexpr auto config = SafeStreamConfig{
    20 * 1024,
    10 * 1024,
    100,
    3,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{25},
    std::chrono::milliseconds{80},
};

constexpr auto kTick = std::chrono::milliseconds{1};

class TestSafeStreamActionsTransport {
 public:
  explicit TestSafeStreamActionsTransport(SafeStreamSendAction& send_action,
                                          SafeStreamRecvAction& recv_action)
      : send_action_{&send_action}, recv_action_{&recv_action} {}

  virtual ~TestSafeStreamActionsTransport() = default;

  virtual void PushData(SSRingIndex begin, DataMessage data_message) {
    MakePushData(begin, std::move(data_message));
  }

  virtual void SendAck(SSRingIndex offset) { MakeSendAck(offset); }

  virtual void SendRepeatRequest(SSRingIndex offset) {
    MakeSendRepeatRequest(offset);
  }

  void MakePushData(SSRingIndex begin, DataMessage data_message) {
    recv_action_->PushData(begin, std::move(data_message));
  }

  void MakeSendAck(SSRingIndex offset) { send_action_->Acknowledge(offset); }

  void MakeSendRepeatRequest(SSRingIndex offset) {
    send_action_->RequestRepeat(offset);
  }

  SafeStreamSendAction* send_action_;
  SafeStreamRecvAction* recv_action_;
};

class DelayPushDataImpl : public TestSafeStreamActionsTransport {
 public:
  struct PushMessageT {
    SSRingIndex begin;
    DataMessage message;
  };

  using TestSafeStreamActionsTransport::TestSafeStreamActionsTransport;

  void PushData(SSRingIndex begin, DataMessage data_message) override {
    push_message = PushMessageT{begin, std::move(data_message)};
  }

  void MakePushData() {
    TEST_ASSERT_TRUE(push_message.has_value());
    auto msg = *push_message;
    TestSafeStreamActionsTransport::MakePushData(msg.begin,
                                                 std::move(msg.message));
  }

  std::optional<PushMessageT> push_message;
};

class MockSendDataPush : public ISendDataPush {
  class DoneStreamWriteAction : public StreamWriteAction {
   public:
    DoneStreamWriteAction(ActionContext action_context)
        : StreamWriteAction{action_context} {
      state_ = State::kDone;
    }

    using StreamWriteAction::StreamWriteAction;
    void Stop() override { state_ = State::kStopped; }
  };

 public:
  MockSendDataPush(ActionContext action_context)
      : action_context_{action_context} {}

  void Link(TestSafeStreamActionsTransport& transport) {
    transport_ = &transport;
  }

  ActionPtr<StreamWriteAction> PushData(SSRingIndex begin,
                                        DataMessage&& data_message) {
    transport_->PushData(begin, std::move(data_message));
    return ActionPtr<DoneStreamWriteAction>{action_context_};
  }

  ActionContext action_context_;
  TestSafeStreamActionsTransport* transport_;
};

class MockSendAckRepeat : public ISendAckRepeat {
 public:
  MockSendAckRepeat() = default;

  void Link(TestSafeStreamActionsTransport& transport) {
    transport_ = &transport;
  }

  void SendAck(SSRingIndex offset) override { transport_->SendAck(offset); }
  void SendRepeatRequest(SSRingIndex offset) override {
    transport_->SendRepeatRequest(offset);
  }

  TestSafeStreamActionsTransport* transport_;
};

static constexpr std::string_view test_data =
    "If it works, it works! If it doesn't, it doesn't!";

/**
 * \brief Test SafeStreamSendAction could send a packet to SafeStreamRecvAction.
 * SafeStreamSendAction starts sending with a random offset and reset flag set
 * to 1. SafeStreamRecvAction starts with unknown offset and set it to the value
 * of the first packet received.
 */
void test_SafeStreamInitHandshake() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  bool acked{};
  DataBuffer received{};

  auto send_transport = MockSendDataPush{ac};
  auto recv_transport = MockSendAckRepeat{};

  auto sender = ActionPtr<SafeStreamSendAction>{ac, send_transport, config};
  sender->SetMaxPayload(config.max_packet_size);
  auto receiver = ActionPtr<SafeStreamRecvAction>{ac, recv_transport, config};

  auto sender_to_receiver = TestSafeStreamActionsTransport{*sender, *receiver};
  send_transport.Link(sender_to_receiver);
  recv_transport.Link(sender_to_receiver);

  receiver->receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  auto send_data = sender->SendData(ToDataBuffer(test_data));

  send_data->ResultEvent().Subscribe([&](auto const&) { acked = true; });

  ap.Update(epoch);
  ap.Update(epoch += config.send_ack_timeout + kTick);

  // test received data
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());
  TEST_ASSERT_TRUE(acked);
}

/**
 * \brief Test SafeStreamSendAction could be recreated and still be able to send
 * data to SafeStreamRecvAction.
 */
void test_SafeStreamReInitSender() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  bool acked{};
  DataBuffer received{};

  auto send_transport = MockSendDataPush{ac};
  auto recv_transport = MockSendAckRepeat{};

  // sender is optional and will be replaced
  auto sender = ActionPtr<SafeStreamSendAction>{ac, send_transport, config};
  sender->SetMaxPayload(config.max_packet_size);
  auto receiver = ActionPtr<SafeStreamRecvAction>{ac, recv_transport, config};

  auto sender_to_receiver = TestSafeStreamActionsTransport{*sender, *receiver};
  send_transport.Link(sender_to_receiver);
  recv_transport.Link(sender_to_receiver);

  receiver->receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  auto send_action1 = sender->SendData(ToDataBuffer(test_data));
  send_action1->ResultEvent().Subscribe([&](auto const&) { acked = true; });

  ap.Update(epoch);
  ap.Update(epoch += kTick);
  ap.Update(epoch += config.send_ack_timeout + kTick);

  // test received data
  TEST_ASSERT_TRUE(acked);
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());

  acked = false;
  received.clear();

  // create new sender
  sender = ActionPtr<SafeStreamSendAction>{ac, send_transport, config};
  sender->SetMaxPayload(config.max_packet_size);

  sender_to_receiver = TestSafeStreamActionsTransport{*sender, *receiver};
  send_transport.Link(sender_to_receiver);
  recv_transport.Link(sender_to_receiver);

  auto send_action2 = sender->SendData(ToDataBuffer(test_data));
  send_action2->ResultEvent().Subscribe([&](auto const&) { acked = true; });

  ap.Update(epoch);
  ap.Update(epoch += kTick);
  ap.Update(epoch += config.send_ack_timeout + kTick);

  // test received data
  TEST_ASSERT_TRUE(acked);
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());
}

/**
 * \brief Same as test_SafeStreamReInitSender but with receiver recreation.
 */
void test_SafeStreamReInitReceiver() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  bool acked{};
  DataBuffer received{};

  auto send_transport = MockSendDataPush{ac};
  auto recv_transport = MockSendAckRepeat{};

  // sender is optional and will be replaced
  auto sender = ActionPtr<SafeStreamSendAction>{ac, send_transport, config};
  sender->SetMaxPayload(config.max_packet_size);
  auto receiver = ActionPtr<SafeStreamRecvAction>{ac, recv_transport, config};

  auto sender_to_receiver = TestSafeStreamActionsTransport{*sender, *receiver};
  send_transport.Link(sender_to_receiver);
  recv_transport.Link(sender_to_receiver);

  receiver->receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  auto send_action1 = sender->SendData(ToDataBuffer(test_data));
  send_action1->ResultEvent().Subscribe([&](auto const&) { acked = true; });

  ap.Update(epoch);
  ap.Update(epoch += config.send_ack_timeout + kTick);

  // test received data
  TEST_ASSERT_TRUE(acked);
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());

  acked = false;
  received.clear();

  // create new sender
  receiver = ActionPtr<SafeStreamRecvAction>{ac, recv_transport, config};

  sender_to_receiver = TestSafeStreamActionsTransport{*sender, *receiver};
  send_transport.Link(sender_to_receiver);
  recv_transport.Link(sender_to_receiver);
  receiver->receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  auto send_action2 = sender->SendData(ToDataBuffer(test_data));
  send_action2->ResultEvent().Subscribe([&](auto const&) { acked = true; });

  ap.Update(epoch);
  ap.Update(epoch += config.send_ack_timeout + kTick);

  // test received data
  TEST_ASSERT_TRUE(acked);
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());
}
}  // namespace ae::test_safe_stream_send_recv

int test_safe_stream_send_recv() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_send_recv::test_SafeStreamInitHandshake);
  RUN_TEST(ae::test_safe_stream_send_recv::test_SafeStreamReInitSender);
  RUN_TEST(ae::test_safe_stream_send_recv::test_SafeStreamReInitReceiver);
  return UNITY_END();
}

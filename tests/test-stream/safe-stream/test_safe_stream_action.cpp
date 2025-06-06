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

#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/api_protocol/api_context.h"
#include "aether/stream_api/safe_stream/safe_stream_action.h"

#include "tests/test-stream/to_data_buffer.h"
#include "tests/test-stream/mock_write_stream.h"

namespace ae::test_safe_stream_action {
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

class TestSafeStreamApiImpl : public SafeStreamApiImpl {
 public:
  explicit TestSafeStreamApiImpl(MockWriteStream& source, MockWriteStream& dst)
      : source_stream{&source},
        dst_stream{&dst},
        safe_stream_api{protocol_context, *this},
        on_data_sub{
            source_stream->on_write_event().Subscribe([&](auto const& data) {
              auto api_parser = ApiParser{protocol_context, data};
              api_parser.Parse(safe_stream_api);
            })} {}

  void Init(RequestId req_id, std::uint16_t repeat_count,
            SafeStreamInit safe_stream_init) override {
    MakeInit(req_id, repeat_count, safe_stream_init);
  }
  void InitAck(RequestId req_id, SafeStreamInit safe_stream_init) override {
    MakeInitAck(req_id, safe_stream_init);
  }
  void Confirm(std::uint16_t offset) override { MakeConfirm(offset); }
  void RequestRepeat(std::uint16_t offset) override {
    MakeRequestRepeat(offset);
  }
  void Send(std::uint16_t offset, DataBuffer&& data) override {
    MakeSend(offset, std::move(data));
  }
  void Repeat(std::uint16_t repeat_count, std::uint16_t offset,
              DataBuffer&& data) override {
    MakeRepeat(repeat_count, offset, std::move(data));
  }

  virtual void MakeInit(RequestId req_id, std::uint16_t repeat_count,
                        SafeStreamInit safe_stream_init) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->init(req_id, repeat_count, safe_stream_init);
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeInitAck(RequestId req_id, SafeStreamInit safe_stream_init) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->init_ack(req_id, safe_stream_init);
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeConfirm(std::uint16_t offset) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->confirm(offset);
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeRequestRepeat(std::uint16_t offset) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->request_repeat(offset);
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeSend(std::uint16_t offset, DataBuffer&& data) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->send(offset, std::move(data));
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeRepeat(std::uint16_t repeat_count, std::uint16_t offset,
                          DataBuffer&& data) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->repeat(repeat_count, offset, std::move(data));
    dst_stream->WriteOut(std::move(api));
  }

  MockWriteStream* source_stream;
  MockWriteStream* dst_stream;
  ProtocolContext protocol_context;
  SafeStreamApi safe_stream_api;
  Subscription on_data_sub;
};

class DelaySendImpl : public TestSafeStreamApiImpl {
 public:
  struct SendT {
    std::uint16_t offset;
    DataBuffer data;
  };
  struct RepeatSendT {
    std::uint16_t repeat_count;
    std::uint16_t offset;
    DataBuffer data;
  };

  using TestSafeStreamApiImpl::TestSafeStreamApiImpl;

  void Send(std::uint16_t offset, DataBuffer&& data) override {
    send = SendT{offset, std::move(data)};
  }

  void Repeat(std::uint16_t repeat_count, std::uint16_t offset,
              DataBuffer&& data) override {
    repeat_send = RepeatSendT{repeat_count, offset, std::move(data)};
  }

  void MakeSend() {
    TEST_ASSERT_TRUE(send.has_value());
    TestSafeStreamApiImpl::MakeSend(send->offset, std::move(send->data));
  }

  void MakeRepeat() {
    TEST_ASSERT_TRUE(repeat_send.has_value());
    TestSafeStreamApiImpl::MakeRepeat(repeat_send->repeat_count,
                                      repeat_send->offset,
                                      std::move(repeat_send->data));
  }

  std::optional<SendT> send;
  std::optional<RepeatSendT> repeat_send;
};

class DelayInitImpl : public DelaySendImpl {
 public:
  using DelaySendImpl::DelaySendImpl;

  void Init(RequestId req_id, std::uint16_t repeat_count,
            SafeStreamInit safe_stream_init) override {
    init_ = InitT{req_id, repeat_count, safe_stream_init};
  }

  void InitAck(RequestId req_id, SafeStreamInit safe_stream_init) override {
    init_ack_ = InitAckT{req_id, safe_stream_init};
  }

  void MakeInit() {
    TEST_ASSERT_TRUE(init_.has_value());
    TestSafeStreamApiImpl::MakeInit(init_->req_id, init_->repeat_count,
                                    init_->safe_stream_init);
  }
  void MakeInitAck() {
    TEST_ASSERT_TRUE(init_ack_.has_value());
    TestSafeStreamApiImpl::MakeInitAck(init_ack_->req_id,
                                       init_ack_->safe_stream_init);
  }

  struct InitT {
    RequestId req_id;
    std::uint16_t repeat_count;
    SafeStreamInit safe_stream_init;
  };

  struct InitAckT {
    RequestId req_id;
    SafeStreamInit safe_stream_init;
  };

  std::optional<InitT> init_;
  std::optional<InitAckT> init_ack_;
};

class ApiProvider final : public SafeStreamApiProvider {
 public:
  explicit ApiProvider(ByteIStream& stream) : stream_{&stream} {}

  ApiCallAdapter<SafeStreamApi> safe_stream_api() override {
    return ApiCallAdapter{ApiContext{*protocol_context_, *api_}, *stream_};
  }

  void SetProtocol(ProtocolContext& protocol_context, SafeStreamApi& api) {
    protocol_context_ = &protocol_context;
    api_ = &api;
  }

 private:
  ProtocolContext* protocol_context_;
  SafeStreamApi* api_;
  ByteIStream* stream_;
};

class TestSafeStreamAction {
 public:
  static constexpr std::size_t kStreamSize = 100;

  explicit TestSafeStreamAction(ActionContext ac)
      : action_context{ac},
        stream{action_context, kStreamSize},
        api_provider{stream},
        safe_stream_action{action_context, api_provider, config},
        safe_stream_api{protocol_context, safe_stream_action} {
    api_provider.SetProtocol(protocol_context, safe_stream_api);
    safe_stream_action.set_max_data_size(kStreamSize);

    stream.out_data_event().Subscribe([&](auto const& data) {
      auto api_parser = ApiParser{protocol_context, data};
      api_parser.Parse(safe_stream_api);
    });
  }

  ActionContext action_context;
  MockWriteStream stream;
  ApiProvider api_provider;
  SafeStreamAction safe_stream_action;
  ProtocolContext protocol_context;
  SafeStreamApi safe_stream_api;
};

static constexpr std::string_view test_data =
    "If it works, it works! If it doesn't, it doesn't!";

void test_SafeStreamInitHandshake() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  DataBuffer received{};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};
  auto sender_to_receiver =
      TestSafeStreamApiImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  test_ssa_receiver.safe_stream_action.receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));

  ap.Update(epoch);
  ap.Update(epoch);

  // test if handshake sucsseeded
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());

  // test received data
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());
}

void test_SafeStreamInitDelay() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      DelayInitImpl(test_ssa_sender.stream, test_ssa_receiver.stream);
  auto receiver_to_sender =
      DelayInitImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  sender_to_receiver.MakeInit();
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitAck,
                    test_ssa_receiver.safe_stream_action.state());

  ap.Update(epoch);

  receiver_to_sender.MakeInitAck();
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());
}

void test_SafeStreamReInit() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      DelayInitImpl(test_ssa_sender.stream, test_ssa_receiver.stream);
  auto receiver_to_sender =
      DelayInitImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  TEST_ASSERT_TRUE(sender_to_receiver.init_.has_value());
  sender_to_receiver.init_.reset();

  ap.Update(epoch += config.wait_confirm_timeout - kTick);
  TEST_ASSERT_FALSE(sender_to_receiver.init_.has_value());

  ap.Update(epoch += config.wait_confirm_timeout + kTick);
  TEST_ASSERT_TRUE(sender_to_receiver.init_.has_value());

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());
  sender_to_receiver.MakeInit();
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitAck,
                    test_ssa_receiver.safe_stream_action.state());

  ap.Update(epoch);
  TEST_ASSERT_TRUE(receiver_to_sender.init_ack_.has_value());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());

  receiver_to_sender.MakeInitAck();
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
}

void test_SafeStreamReInitInitiated() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  DataBuffer received = {};

  // make optional to replace after
  auto test_ssa_sender = std::optional<TestSafeStreamAction>{std::in_place, ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver = std::optional<TestSafeStreamApiImpl>{
      std::in_place, test_ssa_sender->stream, test_ssa_receiver.stream};
  auto receiver_to_sender = std::optional<TestSafeStreamApiImpl>{
      std::in_place, test_ssa_receiver.stream, test_ssa_sender->stream};

  test_ssa_receiver.safe_stream_action.receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender->safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver.safe_stream_action.state());

  test_ssa_sender->safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);
  ap.Update(epoch += kTick);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender->safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  received.clear();

  // make new sender
  test_ssa_sender.emplace(ac);

  sender_to_receiver.emplace(test_ssa_sender->stream, test_ssa_receiver.stream);
  receiver_to_sender.emplace(test_ssa_receiver.stream, test_ssa_sender->stream);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender->safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());

  test_ssa_sender->safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch += kTick);
  ap.Update(epoch += kTick);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender->safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
}

void test_SafeStreamReInitReceiver() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  // make optional to replace after
  auto test_ssa_receiver =
      std::optional<TestSafeStreamAction>{std::in_place, ac};

  auto sender_to_receiver = std::optional<TestSafeStreamApiImpl>{
      std::in_place, test_ssa_sender.stream, test_ssa_receiver->stream};
  auto receiver_to_sender = std::optional<TestSafeStreamApiImpl>{
      std::in_place, test_ssa_receiver->stream, test_ssa_sender.stream};

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver->safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver->safe_stream_action.state());

  // make new receiver
  test_ssa_receiver.emplace(ac);

  sender_to_receiver.emplace(test_ssa_sender.stream, test_ssa_receiver->stream);
  receiver_to_sender.emplace(test_ssa_receiver->stream, test_ssa_sender.stream);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_receiver->safe_stream_action.state());

  // send new data
  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver->safe_stream_action.state());
}

void test_SafeStreamReInitAck() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto sender_to_sender =
      DelayInitImpl{test_ssa_sender.stream, test_ssa_sender.stream};

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  sender_to_sender.TestSafeStreamApiImpl::MakeInitAck(
      sender_to_sender.init_->req_id, sender_to_sender.init_->safe_stream_init);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());

  // repeat InitAck
  sender_to_sender.TestSafeStreamApiImpl::MakeInitAck(
      sender_to_sender.init_->req_id + 1,
      {0, sender_to_sender.init_->safe_stream_init.window_size,
       sender_to_sender.init_->safe_stream_init.max_packet_size});

  // still the same
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
}

void test_SafeStreamInitAckByConfirm() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto sender_to_sender =
      DelayInitImpl{test_ssa_sender.stream, test_ssa_sender.stream};

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());

  // send wrong confirm
  sender_to_sender.TestSafeStreamApiImpl::MakeConfirm(
      sender_to_sender.send->offset + config.window_size + 1);
  // not initiated
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());

  // send right confirm
  sender_to_sender.TestSafeStreamApiImpl::MakeConfirm(
      sender_to_sender.send->offset);
  // initiated
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
}

}  // namespace ae::test_safe_stream_action

int test_safe_stream_action() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamInitHandshake);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamInitDelay);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamReInit);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamReInitInitiated);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamReInitReceiver);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamReInitAck);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamInitAckByConfirm);
  return UNITY_END();
}

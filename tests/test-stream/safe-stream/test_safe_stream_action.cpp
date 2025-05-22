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

  void Init(RequestId req_id, std::uint16_t offset, std::uint16_t window_size,
            std::uint16_t max_packet_size) override {
    MakeInit(req_id, offset, window_size, max_packet_size);
  }
  void InitAck(RequestId req_id, std::uint16_t offset,
               std::uint16_t window_size,
               std::uint16_t max_packet_size) override {
    MakeInitAck(req_id, offset, window_size, max_packet_size);
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

  virtual void MakeInit(RequestId req_id, std::uint16_t offset,
                        std::uint16_t window_size,
                        std::uint16_t max_packet_size) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->init(req_id, offset, window_size, max_packet_size);
    dst_stream->WriteOut(std::move(api));
  }
  virtual void MakeInitAck(RequestId req_id, std::uint16_t offset,
                           std::uint16_t window_size,
                           std::uint16_t max_packet_size) {
    auto api = ApiContext{protocol_context, safe_stream_api};
    api->init_ack(req_id, offset, window_size, max_packet_size);
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

  void Init(RequestId req_id, std::uint16_t offset, std::uint16_t window_size,
            std::uint16_t max_packet_size) override {
    init_ = InitT{req_id, offset, window_size, max_packet_size};
  }

  void InitAck(RequestId req_id, std::uint16_t offset,
               std::uint16_t window_size,
               std::uint16_t max_packet_size) override {
    init_ack_ = InitAckT{req_id, offset, window_size, max_packet_size};
  }

  void MakeInit() {
    TEST_ASSERT_TRUE(init_.has_value());
    TestSafeStreamApiImpl::MakeInit(init_->req_id_, init_->offset_,
                                    init_->window_size_,
                                    init_->max_packet_size_);
  }
  void MakeInitAck() {
    TEST_ASSERT_TRUE(init_ack_.has_value());
    TestSafeStreamApiImpl::MakeInitAck(init_ack_->req_id_, init_ack_->offset_,
                                       init_ack_->window_size_,
                                       init_ack_->max_packet_size_);
  }

  struct InitT {
    RequestId req_id_;
    std::uint16_t offset_;
    std::uint16_t window_size_;
    std::uint16_t max_packet_size_;
  };

  struct InitAckT {
    RequestId req_id_;
    std::uint16_t offset_;
    std::uint16_t window_size_;
    std::uint16_t max_packet_size_;
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

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));

  ap.Update(epoch);
  ap.Update(epoch);

  // test if handshake sucsseeded
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_receiver.safe_stream_action.state());

  // test updated offset
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        test_ssa_sender.safe_stream_action.send_state().begin));
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_sent));
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_added));

  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());

  ap.Update(epoch += config.send_confirm_timeout + kTick);
  ap.Update(epoch += kTick);

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().begin));
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
  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInit,
                    test_ssa_sender.safe_stream_action.state());

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kWaitInitAck,
                    test_ssa_sender.safe_stream_action.state());
  sender_to_sender.TestSafeStreamApiImpl::MakeInitAck(
      sender_to_sender.init_->req_id_, sender_to_sender.init_->offset_,
      sender_to_sender.init_->window_size_,
      sender_to_sender.init_->max_packet_size_);

  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());

  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        test_ssa_sender.safe_stream_action.send_state().begin));

  // repeat InitAck
  sender_to_sender.TestSafeStreamApiImpl::MakeInitAck(
      sender_to_sender.init_->req_id_ + 1, 0,
      sender_to_sender.init_->window_size_,
      sender_to_sender.init_->max_packet_size_);

  // still the same
  TEST_ASSERT_EQUAL(SafeStreamAction::State::kInitiated,
                    test_ssa_sender.safe_stream_action.state());

  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        test_ssa_sender.safe_stream_action.send_state().begin));
}

void test_SafeStreamInitAckByConfirm() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto sender_to_sender =
      DelayInitImpl{test_ssa_sender.stream, test_ssa_sender.stream};
  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

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

  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        test_ssa_sender.safe_stream_action.send_state().begin));
}

void test_SafeStreamRepeatSend() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  DataBuffer received{};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      DelaySendImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  test_ssa_receiver.safe_stream_action.receive_event().Subscribe(
      [&](auto const& data) { received = data; });

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  ap.Update(epoch);

  // begin should stay the same untile confirmation
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        test_ssa_sender.safe_stream_action.send_state().begin));
  // last sent should be moved to data.size()
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_sent));

  TEST_ASSERT_TRUE(sender_to_receiver.send.has_value());
  // receiver has not received
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  // one update to detect repeat
  ap.Update(epoch += config.wait_confirm_timeout + kTick);
  // second to send repeat
  ap.Update(epoch += kTick);

  TEST_ASSERT_TRUE(sender_to_receiver.repeat_send.has_value());
  // receiver has not received
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  sender_to_receiver.MakeSend();
  // update to receive data
  ap.Update(epoch += kTick);
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));
  TEST_ASSERT_EQUAL(test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), received.data(),
                               test_data.size());
  received.clear();

  sender_to_receiver.MakeRepeat();
  // update to receive data
  ap.Update(epoch += kTick);
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));
  // repeat data hasn't received
  TEST_ASSERT_EQUAL(0, received.size());
}

void test_SafeStreamSendErrorOnRepeatExceed() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto wait_confirm_timeout = [](int count) {
    return std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(
            config.wait_confirm_timeout.count() *
            ((count > 0) ? (AE_SAFE_STREAM_RTO_GROW_FACTOR * count) : 1))};
  };

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      DelaySendImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  bool sucsseeded = false;
  bool failed = false;

  auto send_action =
      test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  send_action->ResultEvent().Subscribe([&](auto const&) { sucsseeded = true; });
  send_action->ErrorEvent().Subscribe([&](auto const&) { failed = true; });

  ap.Update(epoch);

  TEST_ASSERT_TRUE(sender_to_receiver.send.has_value());
  // receiver has not received
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  // one update to detect repeat
  ap.Update(epoch += wait_confirm_timeout(0) + kTick);
  // second to send repeat
  ap.Update(epoch += kTick);

  TEST_ASSERT_TRUE(sender_to_receiver.repeat_send.has_value());
  sender_to_receiver.repeat_send.reset();

  // one update to detect repeat
  ap.Update(epoch += wait_confirm_timeout(1) + kTick);
  // second to send repeat
  ap.Update(epoch += kTick);
  TEST_ASSERT_TRUE(sender_to_receiver.repeat_send.has_value());
  sender_to_receiver.repeat_send.reset();

  // one update to detect repeat
  ap.Update(epoch += wait_confirm_timeout(2) + kTick);
  // second to send repeat
  ap.Update(epoch += kTick);

  TEST_ASSERT_FALSE(sender_to_receiver.repeat_send.has_value());
  TEST_ASSERT_FALSE(sucsseeded);
  TEST_ASSERT_TRUE(failed);
}

void test_SafeStreamSendStopped() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      DelaySendImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  bool sucsseeded = false;
  bool stopped1 = false;
  bool stopped2 = false;

  auto send_action1 =
      test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  send_action1->ResultEvent().Subscribe(
      [&](auto const&) { sucsseeded = true; });
  send_action1->StopEvent().Subscribe([&](auto const&) { stopped1 = true; });

  ap.Update(epoch);

  auto send_action2 =
      test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  send_action2->ResultEvent().Subscribe(
      [&](auto const&) { sucsseeded = true; });
  send_action2->StopEvent().Subscribe([&](auto const&) { stopped2 = true; });

  TEST_ASSERT_TRUE(sender_to_receiver.send.has_value());
  // receiver has not received
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  send_action1->Stop();
  send_action2->Stop();

  ap.Update(epoch += kTick);

  // unable to stop sending data
  TEST_ASSERT_FALSE(sucsseeded);
  TEST_ASSERT_FALSE(stopped1);

  // not sending data is stopped
  TEST_ASSERT_FALSE(sucsseeded);
  TEST_ASSERT_TRUE(stopped2);
}

void test_SafeStreamKeepSendingAfterStop() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      TestSafeStreamApiImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  bool sucsseeded1 = false;
  bool sucsseeded2 = false;
  bool stopped1 = false;
  bool stopped2 = false;

  auto send_action1 =
      test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  send_action1->ResultEvent().Subscribe(
      [&](auto const&) { sucsseeded1 = true; });
  send_action1->StopEvent().Subscribe([&](auto const&) { stopped1 = true; });

  send_action1->Stop();

  ap.Update(epoch);

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_added));
  // nothing is sent
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_sent));

  auto send_action2 =
      test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(test_data));
  send_action2->ResultEvent().Subscribe(
      [&](auto const&) { sucsseeded2 = true; });
  send_action2->StopEvent().Subscribe([&](auto const&) { stopped2 = true; });

  ap.Update(epoch += kTick);
  ap.Update(epoch += config.send_confirm_timeout + kTick);
  ap.Update(epoch += kTick);

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_added));

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_sent));

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  TEST_ASSERT_FALSE(sucsseeded1);
  TEST_ASSERT_TRUE(stopped1);

  TEST_ASSERT_TRUE(sucsseeded2);
  TEST_ASSERT_FALSE(stopped2);
}

static constexpr std::string_view large_test_data =
    "In 2099, software engineers coded with thoughts. One dev accidentally "
    "deployed a meme to Mars rovers. They laughed for 3 sols before rebooting. "
    "AI PMs demanded 200% productivity. Engineers rebelled, forming the Bug "
    "Alliance. Their motto: 'It’s not a bug, it’s a rebellion.' They now code "
    "in hex for style. Meetings are banned.";

void test_SafeStreamSendMoreDataThanPacketSize() {
  auto epoch = Now();
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  DataBuffer received = {};

  auto test_ssa_sender = TestSafeStreamAction{ac};
  auto test_ssa_receiver = TestSafeStreamAction{ac};

  auto sender_to_receiver =
      TestSafeStreamApiImpl{test_ssa_sender.stream, test_ssa_receiver.stream};
  auto receiver_to_sender =
      TestSafeStreamApiImpl{test_ssa_receiver.stream, test_ssa_sender.stream};

  test_ssa_receiver.safe_stream_action.receive_event().Subscribe(
      [&](auto const& data) {
        received.insert(std::end(received), std::begin(data), std::end(data));
      });

  auto begin_offset = test_ssa_sender.safe_stream_action.send_state().begin;

  test_ssa_sender.safe_stream_action.SendData(ToDataBuffer(large_test_data));

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + large_test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_sender.safe_stream_action.send_state().last_added));

  constexpr auto packet_size = config.max_data_size - 16;

  for (auto i = 0; i < large_test_data.size() / packet_size; ++i) {
    ap.Update(epoch += kTick);
    TEST_ASSERT_EQUAL(
        static_cast<SSRingIndex::type>(begin_offset + (packet_size * (i + 1))),
        static_cast<SSRingIndex::type>(
            test_ssa_sender.safe_stream_action.send_state().last_sent));
  }
  ap.Update(epoch += config.send_confirm_timeout + kTick);
  ap.Update(epoch += kTick);

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + large_test_data.size()),
      static_cast<SSRingIndex::type>(
          test_ssa_receiver.safe_stream_action.receive_state().last_emitted));

  TEST_ASSERT_EQUAL(large_test_data.size(), received.size());
  TEST_ASSERT_EQUAL_STRING_LEN(large_test_data.data(), received.data(),
                               large_test_data.size());
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
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamRepeatSend);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamSendErrorOnRepeatExceed);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamSendStopped);
  RUN_TEST(ae::test_safe_stream_action::test_SafeStreamKeepSendingAfterStop);
  RUN_TEST(
      ae::test_safe_stream_action::test_SafeStreamSendMoreDataThanPacketSize);
  return UNITY_END();
}

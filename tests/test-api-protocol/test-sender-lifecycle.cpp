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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/make_api_call_sender.h"
#include "aether/events/events.h"
#include "aether/executors/executors.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae::test_api_protocol_sender_lifecycle {

constexpr std::int32_t kResultValue{42};
constexpr std::int32_t kApiErrorValue{-42};
constexpr std::int32_t kIgnoredApiError{-1};
constexpr std::int32_t kFailureResultValue{7};
constexpr std::int32_t kStoppedResultValue{8};
constexpr std::int32_t kNoRequestError{0};
constexpr std::uint32_t kNotifyMethodId{2};
constexpr std::uint32_t kFirstRequestId{1};
constexpr std::uint32_t kSecondRequestId{2};
constexpr int kNoWrites{0};
constexpr int kOneWrite{1};
constexpr int kTwoWrites{2};
constexpr int kOneCompletion{1};

class SenderApi : public DeclareApi<SenderApi> {
 public:
  virtual ~SenderApi() = default;

  virtual void Notify() = 0;

  API_LIST(METHOD(kNotifyMethodId, Notify))
};

class ClientSenderApi final : public SenderApi {
 public:
  void Notify() override { ClientMethod<&SenderApi::Notify>(); }
};

struct SenderFixture {
  mutable TaskScheduler scheduler_;
  mutable EventSystem event_system_;
  ProtocolContext protocol_context{scheduler_, event_system_};

  TaskScheduler& scheduler() const { return scheduler_; }
  EventSystem& event_system() const { return event_system_; }
};

class TestWriteAction final : public WriteAction {
 public:
  explicit TestWriteAction(SenderFixture const& fixture)
      : WriteAction{fixture_context(fixture)} {}

  void Complete(Status status) { SetStatus(status); }

 private:
  struct Context {
    explicit Context(SenderFixture const& fixture) : fixture_{&fixture} {}

    TaskScheduler& scheduler() const { return fixture_->scheduler(); }
    EventSystem& event_system() const { return fixture_->event_system(); }

   private:
    SenderFixture const* fixture_;
  };

  static Context fixture_context(SenderFixture const& fixture) {
    return Context{fixture};
  }
};

class TestStream {
 public:
  explicit TestStream(SenderFixture const& fixture) : fixture_{&fixture} {}

  WriteAction& Write(DataBuffer&& data) {
    written_data.emplace(std::move(data));
    ++write_count;
    auto& action = write_action.emplace(*fixture_);
    return action;
  }

  void CompleteWrite(WriteAction::Status status) {
    assert(write_action.has_value() && "Write action must be available");
    write_action->Complete(status);
  }

  int write_count{};
  std::optional<TestWriteAction> write_action;
  std::optional<DataBuffer> written_data;

 private:
  SenderFixture const* fixture_;
};

enum class Completion : std::uint8_t {
  kNone,
  kValue,
  kApiError,
  kWriteError,
  kStopped
};

struct ReceiverState {
  Completion completion{Completion::kNone};
  int completion_count{};
  std::int32_t value{};
  std::int32_t api_error{};
};

struct Receiver {
  using receiver_concept = ex::receiver_t;

  void set_value() const&& noexcept {
    state->completion = Completion::kValue;
    ++state->completion_count;
  }
  void set_value(std::int32_t value) const&& noexcept {
    state->completion = Completion::kValue;
    state->value = value;
    ++state->completion_count;
  }
  void set_error(std::int32_t error) const&& noexcept {
    state->completion = Completion::kApiError;
    state->api_error = error;
    ++state->completion_count;
  }
  void set_error(WriteFailed) const&& noexcept {
    state->completion = Completion::kWriteError;
    ++state->completion_count;
  }
  void set_stopped() const&& noexcept {
    state->completion = Completion::kStopped;
    ++state->completion_count;
  }

  ReceiverState* state;
};

void test_EmptyPromiseDoesNotWriteAndStops() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState state;

  auto sender = make_api_call(
      api, fxtr.protocol_context, stream,
      [](ApiContext<ClientSenderApi>&) { return ApiPromise<std::int32_t>{}; });
  auto operation = ex::connect(std::move(sender), Receiver{&state});
  ex::start(operation);

  TEST_ASSERT_EQUAL(kNoWrites, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kStopped),
                        static_cast<int>(state.completion));
}

void test_FilledPromiseCompletesWithValueAndError() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState success_state;
  ApiFuture<std::int32_t>* success_future = nullptr;

  auto success_sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        success_future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kFirstRequestId);
        return ApiPromise<std::int32_t>{*success_future};
      });
  auto success_operation =
      ex::connect(std::move(success_sender), Receiver{&success_state});
  ex::start(success_operation);
  success_future->OnResult(std::int32_t{kResultValue});

  TEST_ASSERT_EQUAL(kOneWrite, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, success_state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kValue),
                        static_cast<int>(success_state.completion));
  TEST_ASSERT_EQUAL(kResultValue, success_state.value);

  ReceiverState error_state;
  ApiFuture<std::int32_t>* error_future = nullptr;
  auto error_sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        error_future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kSecondRequestId);
        return ApiPromise<std::int32_t>{*error_future};
      });
  auto error_operation =
      ex::connect(std::move(error_sender), Receiver{&error_state});
  ex::start(error_operation);
  error_future->OnError(kNoRequestError, kApiErrorValue);

  TEST_ASSERT_EQUAL(kTwoWrites, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, error_state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kApiError),
                        static_cast<int>(error_state.completion));
  TEST_ASSERT_EQUAL(kApiErrorValue, error_state.api_error);
}

void test_VoidPromiseCompletesWithValue() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState state;
  ApiFuture<void>* future = nullptr;

  auto sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        future = &fxtr.protocol_context.CreateFuture<void>(kFirstRequestId);
        return ApiPromise<void>{*future};
      });
  auto operation = ex::connect(std::move(sender), Receiver{&state});
  ex::start(operation);
  future->OnResult();

  TEST_ASSERT_EQUAL(kOneWrite, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kValue),
                        static_cast<int>(state.completion));
}

void test_EmptyVoidPromiseDoesNotWriteAndStops() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState state;

  auto sender = make_api_call(
      api, fxtr.protocol_context, stream,
      [](ApiContext<ClientSenderApi>&) { return ApiPromise<void>{}; });
  auto operation = ex::connect(std::move(sender), Receiver{&state});
  ex::start(operation);

  TEST_ASSERT_EQUAL(kNoWrites, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kStopped),
                        static_cast<int>(state.completion));
}

void test_ResponseAfterStartCompletesAndReleasesSubscriptions() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState state;
  ApiFuture<std::int32_t>* future = nullptr;

  auto sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kFirstRequestId);
        return ApiPromise<std::int32_t>{*future};
      });
  auto operation = ex::connect(std::move(sender), Receiver{&state});
  ex::start(operation);

  TEST_ASSERT_EQUAL(kOneWrite, stream.write_count);
  TEST_ASSERT_EQUAL(0, state.completion_count);

  future->OnResult(std::int32_t{kResultValue});

  Event<void()> const probe_event{fxtr};
  MultiSubscription probe_subscriptions;
  for (std::size_t i = 0; i < kEventsHandlerCapacity; ++i) {
    probe_subscriptions += probe_event.Subscribe([]() noexcept {});
  }
  TEST_ASSERT_EQUAL(kOneWrite, stream.write_count);
  TEST_ASSERT_EQUAL(kOneCompletion, state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kValue),
                        static_cast<int>(state.completion));
  TEST_ASSERT_EQUAL(kResultValue, state.value);
}

void test_VoidPromiseCallbackIsSupported() {
  using VoidPromiseCallback =
      decltype([](ApiContext<ClientSenderApi>& context) -> ApiPromise<void> {
        context->Notify();
        return {};
      });
  static_assert(make_api_call_sender_internal::ApiCall<VoidPromiseCallback,
                                                       ClientSenderApi>);
  static_assert(requires(ClientSenderApi& api,
                         ProtocolContext& protocol_context,
                         TestStream& stream) {
    make_api_call(api, protocol_context, stream, VoidPromiseCallback{});
  });
}

void test_WriteFailureAndStopCompleteAndReleaseSubscriptions() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState failure_state;
  ApiFuture<std::int32_t>* failure_future = nullptr;

  auto failure_sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        failure_future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kFirstRequestId);
        return ApiPromise<std::int32_t>{*failure_future};
      });
  auto failure_operation =
      ex::connect(std::move(failure_sender), Receiver{&failure_state});
  ex::start(failure_operation);
  stream.CompleteWrite(WriteAction::Status::kFail);
  failure_future->OnResult(std::int32_t{kFailureResultValue});

  TEST_ASSERT_EQUAL(kOneCompletion, failure_state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kWriteError),
                        static_cast<int>(failure_state.completion));

  ReceiverState stopped_state;
  ApiFuture<std::int32_t>* stopped_future = nullptr;
  auto stopped_sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        stopped_future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kSecondRequestId);
        return ApiPromise<std::int32_t>{*stopped_future};
      });
  auto stopped_operation =
      ex::connect(std::move(stopped_sender), Receiver{&stopped_state});
  ex::start(stopped_operation);
  stream.CompleteWrite(WriteAction::Status::kStop);
  stopped_future->OnResult(std::int32_t{kStoppedResultValue});

  TEST_ASSERT_EQUAL(kOneCompletion, stopped_state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kStopped),
                        static_cast<int>(stopped_state.completion));
}

void test_TerminalCompletionIsExactlyOnce() {
  SenderFixture fxtr;
  ClientSenderApi api;
  TestStream stream{fxtr};
  ReceiverState state;
  ApiFuture<std::int32_t>* future = nullptr;

  auto sender = make_api_call(
      api, fxtr.protocol_context, stream, [&](ApiContext<ClientSenderApi>&) {
        future =
            &fxtr.protocol_context.CreateFuture<std::int32_t>(kFirstRequestId);
        return ApiPromise<std::int32_t>{*future};
      });
  auto operation = ex::connect(std::move(sender), Receiver{&state});
  ex::start(operation);
  future->OnResult(std::int32_t{kResultValue});
  stream.CompleteWrite(WriteAction::Status::kFail);
  future->OnError(kNoRequestError, kIgnoredApiError);

  TEST_ASSERT_EQUAL(kOneCompletion, state.completion_count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Completion::kValue),
                        static_cast<int>(state.completion));
  TEST_ASSERT_EQUAL(kResultValue, state.value);
}
}  // namespace ae::test_api_protocol_sender_lifecycle

int test_api_protocol_sender_lifecycle() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_sender_lifecycle;  // NOLINT

  RUN_TEST(test_EmptyPromiseDoesNotWriteAndStops);
  RUN_TEST(test_FilledPromiseCompletesWithValueAndError);
  RUN_TEST(test_VoidPromiseCompletesWithValue);
  RUN_TEST(test_EmptyVoidPromiseDoesNotWriteAndStops);
  RUN_TEST(test_ResponseAfterStartCompletesAndReleasesSubscriptions);
  RUN_TEST(test_VoidPromiseCallbackIsSupported);
  RUN_TEST(test_WriteFailureAndStopCompleteAndReleaseSubscriptions);
  RUN_TEST(test_TerminalCompletionIsExactlyOnce);
  return UNITY_END();
}

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

#include "aether/ae_context.h"
#include "aether/actions/actions_queue.h"
#include "aether/serial_ports/at_support/at_stage.h"
#include "aether/serial_ports/at_support/at_request.h"
#include "aether/serial_ports/at_support/at_support.h"

#include "tests/test-serial-port/mock-serial-port.h"

namespace ae::test_at_stages {
struct TestContext {
  auto ToAeContext() const {
    static constexpr AeCtxTable vtable = {
        .aether_getter = nullptr,
        .scheduler_getter = [](void* obj) -> TaskScheduler& {
          return static_cast<TestContext*>(obj)->scheduler_;
        },
    };
    return AeCtx{
        .obj = const_cast<TestContext*>(this),  // NOLINT
        .vtable = &vtable,
    };
  }

  auto Update(TimePoint tp = Now()) { return scheduler_.Update(tp); }

  TaskScheduler scheduler_;
};

void test_RunAtStage() {
  TestContext ctx;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  ActionsQueue queue;

  DataBuffer received;
  mock_serial.write_event().Subscribe([&](auto data) {
    std::copy(std::begin(data), std::end(data), std::back_inserter(received));
  });

  queue.Push(at::Stage(
      ctx, [&]() { return at::MakeRequest(ex::just(), at_support, "AT"); }));

  ctx.Update();

  TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", received.data(), 5);
}

void test_RunMultipleStages() {
  TestContext ctx;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  ActionsQueue queue;

  std::vector<DataBuffer> received;
  mock_serial.write_event().Subscribe([&](auto data) {
    auto& r = received.emplace_back();
    std::copy(std::begin(data), std::end(data), std::back_inserter(r));
  });

  queue.Push(at::Stage(
      ctx, [&]() { return at::MakeRequest(ex::just(), at_support, "AT"); }));
  queue.Push(at::Stage(
      ctx, [&]() { return at::MakeRequest(ex::just(), at_support, "MT"); }));
  queue.Push(at::Stage(
      ctx, [&]() { return at::MakeRequest(ex::just(), at_support, "UT"); }));

  ctx.Update();

  TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", received[0].data(), 5);
  TEST_ASSERT_EQUAL_STRING_LEN("MT\r\n", received[1].data(), 5);
  TEST_ASSERT_EQUAL_STRING_LEN("UT\r\n", received[2].data(), 5);
}

void test_RunAtWithWaitStage() {
  TestContext ctx;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  ActionsQueue queue;

  mock_serial.write_event().Subscribe([&](auto data) {
    TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", data.data(), 5);
  });

  bool executed = false;
  queue.Push(at::Stage(ctx, [&]() {
    return ex::just() | at::MakeRequest(at_support, "AT", at::Wait{"OK"}) |
           ex::then([&]() noexcept { executed = true; });
  }));

  ctx.Update();
  // waiting for OK
  TEST_ASSERT_FALSE(executed);

  std::string_view ok_line{"OK\r\n"};
  mock_serial.WriteOut(std::span<std::uint8_t const>{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // now executed
  TEST_ASSERT_TRUE(executed);
}

void test_RunMultipleStagesWithWait() {
  TestContext ctx;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  ActionsQueue queue;

  mock_serial.write_event().Subscribe([&](auto data) {
    TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", data.data(), 5);
  });

  bool executed1 = false;
  bool pre_executed2 = false;
  bool post_executed2 = false;
  queue.Push(at::Stage(ctx, [&]() {
    return ex::just() | at::MakeRequest(at_support, "AT", at::Wait{"OK"}) |
           ex::then([&]() noexcept { executed1 = true; });
  }));
  queue.Push(at::Stage(ctx, [&]() {
    return ex::just() | ex::then([&]() noexcept { pre_executed2 = true; }) |
           at::MakeRequest(at_support, "AT", at::Wait{"OK"}) |
           ex::then([&]() noexcept { post_executed2 = true; });
  }));

  ctx.Update();
  // waiting for OK
  TEST_ASSERT_FALSE(executed1);
  TEST_ASSERT_FALSE(pre_executed2);


  std::string_view ok_line{"OK\r\n"};
  mock_serial.WriteOut(std::span<std::uint8_t const>{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});
  // now executed
  TEST_ASSERT_TRUE(executed1);
  // and then start execution next stage
  TEST_ASSERT_TRUE(pre_executed2);
  // but waits for OK
  TEST_ASSERT_FALSE(post_executed2);

  mock_serial.WriteOut(std::span<std::uint8_t const>{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // now finished
  TEST_ASSERT_TRUE(post_executed2);
}

}  // namespace ae::test_at_stages

int test_at_stages() {
  using namespace ae::test_at_stages;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_RunAtStage);
  RUN_TEST(test_RunMultipleStages);
  RUN_TEST(test_RunAtWithWaitStage);
  RUN_TEST(test_RunMultipleStagesWithWait);
  return UNITY_END();
}

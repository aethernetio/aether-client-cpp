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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "aether/ae_context.h"
#include "aether/prepared_packet/package.h"
#include "aether/types/data_buffer.h"

namespace ae::test_package_ptr {
namespace {

struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->sched;
                   }};
    return AeCtx{
        const_cast<TestContext*>(this),  // NOLINT
        &table,
    };
  }

  TaskScheduler sched;
};

prepared_packet::PreparedEndpoint MakeEndpoint() {
  prepared_packet::PreparedEndpoint endpoint;
  endpoint.version = prepared_packet::PreparedIpVersion::kIpV4;
  endpoint.protocol = Protocol::kUdp;
  endpoint.port = 9000;
  endpoint.ip = {127, 0, 0, 1};
  return endpoint;
}

DataBuffer MakePayload() { return DataBuffer{1, 2, 3, 4, 5}; }

}  // namespace

void test_package_ptr_start_success() {
  auto ctx = TestContext{};
  auto payload = MakePayload();
  auto package = prepared_packet::PackagePtr{AeContext{ctx}, MakeEndpoint(),
                                             DataBuffer{payload}};

  TEST_ASSERT_TRUE(static_cast<bool>(package));
  TEST_ASSERT_FALSE(package->is_started());

  auto status = WriteAction::Status::kStop;
  auto status_sub = package->status_event().Subscribe(
      [&](WriteAction::Status s) { status = s; });

  std::size_t send_calls = 0;
  package->Start([&](prepared_packet::PreparedEndpoint const& endpoint,
                     Span<std::uint8_t const> data)
                     -> std::optional<std::size_t> {
        ++send_calls;
        TEST_ASSERT_EQUAL_UINT16(9000, endpoint.port);
        TEST_ASSERT_EQUAL_UINT32(payload.size(), data.size());
        return data.size();
      });

  TEST_ASSERT_TRUE(package->is_started());
  TEST_ASSERT_EQUAL_UINT32(1, send_calls);
  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kSuccess),
                    static_cast<int>(status));
  TEST_ASSERT_TRUE(package->is_finished());
}

void test_package_ptr_start_retry_then_success() {
  auto ctx = TestContext{};
  auto payload = MakePayload();
  auto package = prepared_packet::PackagePtr{AeContext{ctx}, MakeEndpoint(),
                                             DataBuffer{payload}};

  auto status = WriteAction::Status::kStop;
  auto status_sub = package->status_event().Subscribe(
      [&](WriteAction::Status s) { status = s; });

  std::size_t send_calls = 0;
  package->Start([&](prepared_packet::PreparedEndpoint const&,
                     Span<std::uint8_t const> data)
                     -> std::optional<std::size_t> {
        ++send_calls;
        if (send_calls == 1) {
          return 0;
        }
        return data.size();
      });

  TEST_ASSERT_EQUAL_UINT32(1, send_calls);
  TEST_ASSERT_FALSE(package->is_finished());

  ctx.sched.Update();

  TEST_ASSERT_EQUAL_UINT32(2, send_calls);
  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kSuccess),
                    static_cast<int>(status));
  TEST_ASSERT_TRUE(package->is_finished());
}

void test_package_ptr_start_fail() {
  auto ctx = TestContext{};
  auto package = prepared_packet::PackagePtr{AeContext{ctx}, MakeEndpoint(),
                                             MakePayload()};

  auto status = WriteAction::Status::kStop;
  auto status_sub = package->status_event().Subscribe(
      [&](WriteAction::Status s) { status = s; });

  package->Start([](prepared_packet::PreparedEndpoint const&,
                    Span<std::uint8_t const>) -> std::optional<std::size_t> {
    return std::nullopt;
  });

  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kFail),
                    static_cast<int>(status));
  TEST_ASSERT_TRUE(package->is_finished());
}

void test_package_ptr_stop() {
  auto ctx = TestContext{};
  auto package = prepared_packet::PackagePtr{AeContext{ctx}, MakeEndpoint(),
                                             MakePayload()};

  auto status = WriteAction::Status::kSuccess;
  auto status_sub = package->status_event().Subscribe(
      [&](WriteAction::Status s) { status = s; });

  std::size_t send_calls = 0;
  package->Start([&](prepared_packet::PreparedEndpoint const&,
                     Span<std::uint8_t const>) -> std::optional<std::size_t> {
    ++send_calls;
    return 0;
  });

  TEST_ASSERT_EQUAL_UINT32(1, send_calls);
  package->Stop();

  TEST_ASSERT_EQUAL(static_cast<int>(WriteAction::Status::kStop),
                    static_cast<int>(status));
  TEST_ASSERT_TRUE(package->is_finished());

  ctx.sched.Update();
  TEST_ASSERT_EQUAL_UINT32(1, send_calls);
}

}  // namespace ae::test_package_ptr

void test_package_ptr_start_success() {
  ae::test_package_ptr::test_package_ptr_start_success();
}
void test_package_ptr_start_retry_then_success() {
  ae::test_package_ptr::test_package_ptr_start_retry_then_success();
}
void test_package_ptr_start_fail() {
  ae::test_package_ptr::test_package_ptr_start_fail();
}
void test_package_ptr_stop() { ae::test_package_ptr::test_package_ptr_stop(); }

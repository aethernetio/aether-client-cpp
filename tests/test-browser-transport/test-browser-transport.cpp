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

/**
 * Native unit tests for browser_transport_common.h (no Emscripten).
 */

#include <cstdint>
#include <memory>
#include <vector>

#include <unity.h>

#include "aether/transport/browser/browser_transport_common.h"
#include "aether/types/data_buffer.h"
#include "aether/types/packed_size.h"
#include "aether/vector_buffer.h"

namespace ae::test_browser_transport {
namespace {

using browser_transport_internal::BrowserQueueState;
using browser_transport_internal::DestroyAsyncCallbackBridge;
using browser_transport_internal::FrameLengthPrefixedPacket;
using browser_transport_internal::GenerationGuard;
using browser_transport_internal::MakeAsyncCallbackBridge;
using browser_transport_internal::ResolveAsyncTransport;

DataBuffer UnframeLengthPrefixedPacket(DataBuffer framed) {
  VectorBuffer<PacketSize> reader{framed};
  std::size_t payload_size = 0;
  TEST_ASSERT(reader.Read(seri::SizeReadTag{payload_size}));
  DataBuffer payload(payload_size);
  TEST_ASSERT(reader.Read(seri::DataReadTag{payload.data(), payload_size}));
  return payload;
}

}  // namespace

void test_LengthPrefixFramingRoundTripBinaryBytes() {
  DataBuffer payload{0x00, 0x01, 0xff, 0x7e, 0x00, 0xff};
  auto framed = FrameLengthPrefixedPacket(DataBuffer{payload});
  TEST_ASSERT(framed.size() > payload.size());

  auto const recovered = UnframeLengthPrefixedPacket(std::move(framed));
  TEST_ASSERT_EQUAL_UINT(payload.size(), recovered.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), recovered.data(),
                                payload.size());
}

void test_LengthPrefixFramingEmptyPayload() {
  DataBuffer payload{};
  auto framed = FrameLengthPrefixedPacket(DataBuffer{payload});
  auto const recovered = UnframeLengthPrefixedPacket(std::move(framed));
  TEST_ASSERT_EQUAL_UINT(0u, recovered.size());
}

void test_QueueOverflowReturnsFail() {
  BrowserQueueState queue{2};
  TEST_ASSERT_TRUE(queue.TryPush(DataBuffer{0x01}));
  TEST_ASSERT_TRUE(queue.TryPush(DataBuffer{0xff}));
  TEST_ASSERT_TRUE(queue.full());
  TEST_ASSERT_FALSE(queue.TryPush(DataBuffer{0x00}));
  TEST_ASSERT_EQUAL_UINT(2u, queue.size());

  auto first = queue.TryPop();
  TEST_ASSERT_TRUE(first.has_value());
  TEST_ASSERT_EQUAL_UINT8(0x01, (*first)[0]);
  TEST_ASSERT_FALSE(queue.full());
  TEST_ASSERT_TRUE(queue.TryPush(DataBuffer{0xaa}));
}

void test_GenerationGuardInvalidatesPriorToken() {
  GenerationGuard guard;
  auto const shared = guard.shared();
  auto const gen0 = guard.current();
  TEST_ASSERT_TRUE(GenerationGuard::IsCurrent(shared, gen0));

  auto const gen1 = guard.Bump();
  TEST_ASSERT_EQUAL_UINT(gen0 + 1, gen1);
  TEST_ASSERT_FALSE(GenerationGuard::IsCurrent(shared, gen0));
  TEST_ASSERT_TRUE(GenerationGuard::IsCurrent(shared, gen1));
}

struct DummyTransport {
  int value = 42;
};

void test_AsyncCallbackBridgeRejectsDestroyedTransport() {
  DummyTransport transport;
  auto lifetime = std::make_shared<int>(0);
  GenerationGuard guard;
  auto const gen = guard.current();

  auto bridge = MakeAsyncCallbackBridge(lifetime, guard.shared(), &transport);
  auto* bridge_ptr = bridge.get();

  TEST_ASSERT_NOT_NULL(ResolveAsyncTransport<DummyTransport>(
      bridge_ptr, static_cast<std::uint32_t>(gen)));

  DestroyAsyncCallbackBridge(bridge);
  TEST_ASSERT_NULL(ResolveAsyncTransport<DummyTransport>(
      bridge_ptr, static_cast<std::uint32_t>(gen)));

  auto bridge2 = MakeAsyncCallbackBridge(lifetime, guard.shared(), &transport);
  lifetime.reset();
  TEST_ASSERT_NULL(ResolveAsyncTransport<DummyTransport>(
      bridge2.get(), static_cast<std::uint32_t>(gen)));
  DestroyAsyncCallbackBridge(bridge2);
}

}  // namespace ae::test_browser_transport

int test_browser_transport() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_browser_transport::test_LengthPrefixFramingRoundTripBinaryBytes);
  RUN_TEST(ae::test_browser_transport::test_LengthPrefixFramingEmptyPayload);
  RUN_TEST(ae::test_browser_transport::test_QueueOverflowReturnsFail);
  RUN_TEST(ae::test_browser_transport::test_GenerationGuardInvalidatesPriorToken);
  RUN_TEST(
      ae::test_browser_transport::test_AsyncCallbackBridgeRejectsDestroyedTransport);
  return UNITY_END();
}

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

#include <concepts>
#include <type_traits>
#include <utility>

#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/details/packet_reader.h"
#include "aether/stream_api/api_call_adapter.h"

#include "assert_packet.h"

namespace ae::test_api_protocol_packet {

class PacketApi : public DeclareApi<PacketApi> {
 public:
  virtual ~PacketApi() = default;

  virtual void Call(int value) = 0;

  API_LIST(METHOD(3, Call))
};

class ClientPacketApi final : public PacketApi {
 public:
  void Call(int value) override { ClientMethod<&PacketApi::Call>(value); }
};

using PacketList =
    decltype(PacketApi::template MakePacketList<kMaxApiCallPerApiContext>());

template <typename T>
concept CanPackLvalue = requires(T& value) { value.Pack(); };

template <typename T>
concept CanPackConstRvalue =
    requires(T const&& value) { std::move(value).Pack(); };

static_assert(!std::is_copy_constructible_v<PacketList>);
static_assert(!std::is_copy_assignable_v<PacketList>);
static_assert(!std::is_move_constructible_v<PacketList>);
static_assert(!std::is_move_assignable_v<PacketList>);
static_assert(!CanPackLvalue<PacketList>);
static_assert(!CanPackConstRvalue<PacketList>);

static_assert(!std::is_copy_constructible_v<ApiContext<PacketApi>>);
static_assert(!std::is_copy_assignable_v<ApiContext<PacketApi>>);
static_assert(!std::is_move_constructible_v<ApiContext<PacketApi>>);
static_assert(!std::is_move_assignable_v<ApiContext<PacketApi>>);
static_assert(!CanPackLvalue<ApiContext<PacketApi>>);
static_assert(!CanPackConstRvalue<ApiContext<PacketApi>>);

static_assert(!std::is_move_constructible_v<ApiCallAdapter<PacketApi>>);
static_assert(!std::is_move_assignable_v<ApiCallAdapter<PacketApi>>);

void test_DirectContextPacks() {
  auto api = ClientPacketApi{};
  auto context = ApiContext{api};

  context->Call(42);
  auto packet = std::move(context).Pack();

  AssertPacket(packet, MessageId{3}, int{42});
}

void test_TryExtract() {
  auto buffer = DataBuffer{42};
  auto reader = PacketReader{buffer};

  auto message_id = reader.TryExtract<MessageId>();

  TEST_ASSERT_TRUE(message_id.has_value());
  TEST_ASSERT_EQUAL(42, *message_id);
  TEST_ASSERT_FALSE(reader.canceled());
}

void test_FailedTryExtractDoesNotCancel() {
  auto buffer = DataBuffer{};
  auto reader = PacketReader{buffer};

  auto message_id = reader.TryExtract<MessageId>();

  TEST_ASSERT_FALSE(message_id.has_value());
  TEST_ASSERT_FALSE(reader.canceled());
}

void test_Extract() {
  auto buffer = DataBuffer{42};
  auto reader = PacketReader{buffer};

  auto message_id = reader.Extract<MessageId>();

  TEST_ASSERT_EQUAL(42, message_id);
  TEST_ASSERT_FALSE(reader.canceled());
}

#if defined(NDEBUG)
void test_FailedExtractCancelsReader() {
  auto buffer = DataBuffer{};
  auto reader = PacketReader{buffer};

  (void)reader.Extract<MessageId>();

  TEST_ASSERT_TRUE(reader.canceled());
}
#endif
}  // namespace ae::test_api_protocol_packet

int test_api_protocol_packet() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_packet;  // NOLINT

  RUN_TEST(test_DirectContextPacks);
  RUN_TEST(test_TryExtract);
  RUN_TEST(test_FailedTryExtractDoesNotCancel);
  RUN_TEST(test_Extract);
#if defined(NDEBUG)
  RUN_TEST(test_FailedExtractCancelsReader);
#endif
  return UNITY_END();
}

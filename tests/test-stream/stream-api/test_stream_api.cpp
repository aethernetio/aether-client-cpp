/*
 * Copyright 2024 Aethernet Inc.
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

#include <chrono>

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/types/data_buffer.h"

#include "aether/stream_api/stream_api.h"
#include "aether/actions/action_processor.h"

#include "aether/tele/tele_init.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_stream_api {

static constexpr char test_data[] = "Ducks have three eyelids!";

void test_SteamApiMakePacket() {
  auto epoch = TimePoint::clock::now();
  ActionProcessor ap;
  ProtocolContext pc;

  auto written_data = DataBuffer{};
  auto read_data = DataBuffer{};

  std::uint8_t const stream_id = 1;

  auto stream_api_gate = StreamApiGate{pc, stream_id};

  stream_api_gate.out_data_event().Subscribe(
      [&](auto data) { read_data = std::move(data); });

  written_data = stream_api_gate.WriteIn(ToDataBuffer(test_data));

  auto parser = ApiParser{pc, written_data};
  auto api = StreamApi{};
  parser.Parse(api);

  ap.Update(epoch += std::chrono::milliseconds{1});

  TEST_ASSERT_EQUAL(sizeof(test_data) + 3, written_data.size());
  TEST_ASSERT(!read_data.empty());
  TEST_ASSERT_EQUAL_STRING(test_data, read_data.data());
}

}  // namespace ae::test_stream_api

int test_stream_api() {
  ae::tele::TeleInit::Init();

  UNITY_BEGIN();
  RUN_TEST(ae::test_stream_api::test_SteamApiMakePacket);
  return UNITY_END();
}

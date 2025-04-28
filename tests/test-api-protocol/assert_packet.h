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

#ifndef TESTS_TEST_API_PROTOCOL_ASSERT_PACKET_H_
#define TESTS_TEST_API_PROTOCOL_ASSERT_PACKET_H_

#include <unity.h>

#include <vector>
#include <cstdint>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/api_protocol/api_message.h"

#if defined(__clang__) || defined(__GNUC__)
#  define FUNCTION_NAME __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#  define FUNCTION_NAME __FUNCSIG__
#else
#  define FUNCTION_NAME ""
#endif

template <typename T>
struct Skip {};

using imstream_t = ae::imstream<ae::VectorReader<ae::PackedSize>>;

template <typename T>
void AssertPacketEntry(imstream_t& is, T const& t) {
  T temp;
  is >> temp;
  TEST_ASSERT_MESSAGE(t == temp, FUNCTION_NAME);
}

template <typename T>
void AssertPacketEntry(imstream_t& is, Skip<T> const&) {
  T temp;
  is >> temp;
}

template <typename... Args>
void AssertPacket(std::vector<std::uint8_t> const& data, Args const&... args) {
  auto const* old_file = Unity.TestFile;
  UnitySetTestFile(__FILE__);
  ae::VectorReader<ae::PackedSize> ib{data};
  imstream_t is{ib};
  (AssertPacketEntry(is, args), ...);
  TEST_ASSERT_EQUAL_MESSAGE(data.size(), ib.offset_, FUNCTION_NAME);

  UnitySetTestFile(old_file);
}

#endif  // TESTS_TEST_API_PROTOCOL_ASSERT_PACKET_H_ */

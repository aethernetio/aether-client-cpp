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

#ifndef TESTS_TEST_STREAM_TO_DATA_BUFFER_H_
#define TESTS_TEST_STREAM_TO_DATA_BUFFER_H_

#include <vector>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace ae {

template <typename TRes, typename Iterator>
static auto ToVector(Iterator begin, Iterator end) {
  return std::vector<TRes>(begin, end);
}

template <typename T, std::size_t size>
static auto ToDataBuffer(T const (&arr)[size]) {
  return ToVector<std::uint8_t>(std::begin(arr), std::end(arr));
}

static auto ToDataBuffer(std::string_view str) {
  return ToVector<std::uint8_t>(std::begin(str), std::end(str));
}

template <typename Iterator>
static auto ToDataBuffer(Iterator begin, Iterator end) {
  return ToVector<std::uint8_t>(begin, end);
}

}  // namespace ae

#endif  // TESTS_TEST_STREAM_TO_DATA_BUFFER_H_

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

#ifndef TESTS_TEST_STREAM_SAFE_STREAM_TO_DATA_BUFFER_H_
#define TESTS_TEST_STREAM_SAFE_STREAM_TO_DATA_BUFFER_H_

#include <vector>
#include <cstdint>
#include <cstddef>

namespace ae {

template <typename TRes, typename T, std::size_t size>
static auto ToVector(T const (&arr)[size]) {
  return std::vector<TRes>(reinterpret_cast<TRes const*>(arr),
                           reinterpret_cast<TRes const*>(arr + size));
}

template <typename T, std::size_t size>
static auto ToDataBuffer(T const (&arr)[size]) {
  return ToVector<std::uint8_t>(arr);
}

}  // namespace ae

#endif  // TESTS_TEST_STREAM_SAFE_STREAM_TO_DATA_BUFFER_H_

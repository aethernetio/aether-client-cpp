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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIME_TABLE_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIME_TABLE_H_

#include <map>
#include <chrono>
#include <vector>
#include <cstdint>
#include <optional>

#include "aether/common.h"

namespace ae::bench {
using HighResTimePoint =
    std::chrono::time_point<std::chrono::high_resolution_clock>;

using TimeTable = std::map<std::uint32_t, HighResTimePoint>;
using DurationTable =
    std::vector<std::pair<std::uint32_t, std::optional<Duration>>>;
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIME_TABLE_H_

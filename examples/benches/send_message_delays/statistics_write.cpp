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

#include "send_message_delays/statistics_write.h"

namespace ae::bench {
StatisticsWriteCsv::StatisticsWriteCsv(
    std::vector<std::pair<std::string, DurationStatistics>> statistics,
    std::size_t test_msg_count)
    : statistics_{std::move(statistics)}, test_msg_count_{test_msg_count} {}

}  // namespace ae::bench

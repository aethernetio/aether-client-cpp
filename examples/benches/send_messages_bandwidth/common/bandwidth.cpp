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

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
Bandwidth::Bandwidth(Duration duration, std::size_t message_count,
                     std::size_t message_size)
    : duration{duration}, message_count{message_count} {
  using SecDuration = std::chrono::duration<double>;
  auto sec_duration = std::chrono::duration_cast<SecDuration>(this->duration);
  auto message_per_sec =
      static_cast<double>(this->message_count) / sec_duration.count();
  this->bandwidth = message_per_sec * static_cast<double>(8 * message_size);
}
}  // namespace ae::bench

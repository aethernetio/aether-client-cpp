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

#ifndef AETHER_API_PROTOCOL_REQUEST_ID_H_
#define AETHER_API_PROTOCOL_REQUEST_ID_H_

#include <cstdint>

namespace ae {
struct RequestId {
  static auto GenRequestId() {
    static RequestId request_id{1};
    return request_id.id++;
  }

  RequestId() = default;
  RequestId(std::uint16_t id) : id(id) {}

  operator std::uint16_t() const { return id; }

  bool operator==(RequestId const& rhs) const { return id == rhs.id; }
  bool operator!=(RequestId const& rhs) const { return id != rhs.id; }
  bool operator<(RequestId const& rhs) const { return id < rhs.id; }

  template <typename T>
  void Serializator(T& s) {
    s & id;
  }

  std::uint16_t id;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_REQUEST_ID_H_

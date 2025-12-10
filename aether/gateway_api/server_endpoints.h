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

#ifndef AETHER_GATEWAY_API_SERVER_ENDPOINTS_H_
#define AETHER_GATEWAY_API_SERVER_ENDPOINTS_H_

#include <vector>
#include <functional>

#include "aether/crc.h"
#include "aether/mstream.h"
#include "aether/types/address.h"
#include "aether/mstream_buffers.h"
#include "aether/reflect/reflect.h"

namespace ae {
struct ServerEndpoints {
  AE_REFLECT_MEMBERS(endpoints);
  std::vector<Endpoint> endpoints;
};
}  // namespace ae

namespace std {
template <>
struct hash<ae::ServerEndpoints> {
  std::size_t operator()(ae::ServerEndpoints const& endpoints) const {
    std::vector<std::uint8_t> data;
    auto writer = ae::VectorWriter<>{data};
    auto os = ae::omstream{writer};
    os << endpoints;
    return static_cast<std::size_t>(
        crc32::from_buffer(data.data(), data.size()).value);
  }
};
}  // namespace std

#endif  // AETHER_GATEWAY_API_SERVER_ENDPOINTS_H_

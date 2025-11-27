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

#ifndef AETHER_WORK_CLOUD_API_SERVER_DESCRIPTOR_H_
#define AETHER_WORK_CLOUD_API_SERVER_DESCRIPTOR_H_

#include <vector>
#include <functional>

#include "aether/crc.h"
#include "aether/mstream.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/mstream_buffers.h"

namespace ae {

struct CoderAndPort {
  AE_REFLECT_MEMBERS(protocol, port)
  Protocol protocol;
  std::uint16_t port;
};

struct IpAddressAndPort {
  AE_REFLECT_MEMBERS(ip, protocol_and_ports)
  IpAddress ip;
  std::vector<CoderAndPort> protocol_and_ports;
};

struct ServerDescriptor {
  AE_REFLECT_MEMBERS(server_id, ips)
  ServerId server_id;
  std::vector<IpAddressAndPort> ips;
};
}  // namespace ae

namespace std {
template <>
struct hash<ae::ServerDescriptor> {
  std::size_t operator()(ae::ServerDescriptor const& value) {
    std::vector<std::uint8_t> buffer;
    auto bw = ae::VectorWriter<>{buffer};
    auto os = ae::omstream{bw};
    os << value;
    return static_cast<std::size_t>(
        crc32::from_buffer(buffer.data(), buffer.size()).value);
  }
};
}  // namespace std

#endif  // AETHER_WORK_CLOUD_API_SERVER_DESCRIPTOR_H_

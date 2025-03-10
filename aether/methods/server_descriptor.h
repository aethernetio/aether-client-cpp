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

#ifndef AETHER_METHODS_SERVER_DESCRIPTOR_H_
#define AETHER_METHODS_SERVER_DESCRIPTOR_H_

#include <vector>

#include "aether/address.h"

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

#endif  // AETHER_METHODS_SERVER_DESCRIPTOR_H_

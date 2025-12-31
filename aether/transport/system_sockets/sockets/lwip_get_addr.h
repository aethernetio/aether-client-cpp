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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_GET_ADDR_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_GET_ADDR_H_

#if (defined(ESP_PLATFORM))

#  include <optional>

#  include "lwip/ip_addr.h"

#  include "aether/types/address.h"

namespace ae {
std::optional<ip_addr_t> LwipGetAddr(const AddressPort& addr_port);
}  // namespace ae
#endif  // (defined(ESP_PLATFORM))
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_GET_ADDR_H_

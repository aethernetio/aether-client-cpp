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

#ifndef AETHER_TYPES_SERVER_CONFIG_H_
#define AETHER_TYPES_SERVER_CONFIG_H_

#include <vector>

#include "aether/format/format.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/reflect/reflect.h"

namespace ae {
struct ServerConfig {
  AE_REFLECT_MEMBERS(id, endpoints)

  ServerId id;
  std::vector<Endpoint> endpoints;
};

template <>
struct Formatter<ServerConfig> {
  template <typename TStream>
  void Format(ServerConfig const& value, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "server_id={}, endpoints=[{}]", value.id,
               value.endpoints);
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_SERVER_CONFIG_H_

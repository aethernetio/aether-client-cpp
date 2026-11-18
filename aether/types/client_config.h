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

#ifndef AETHER_TYPES_CLIENT_CONFIG_H_
#define AETHER_TYPES_CLIENT_CONFIG_H_

#include <vector>

#include "aether-miscpp/format/format.h"
#include "aether-miscpp/reflect/reflect.h"
#include "aether/crypto/key.h"
#include "aether/peer/peer_types.h"
#include "aether/streams/stream_definition.h"
#include "aether/types/server_config.h"
#include "aether/types/uid.h"

namespace ae {
struct ClientConfig {
  AE_REFLECT_MEMBERS(parent_uid, uid, ephemeral_uid, master_key, cloud,
                     peer_identity, peer_settings, streams)

  Uid parent_uid;
  Uid uid;
  Uid ephemeral_uid;
  Key master_key;
  std::vector<ServerConfig> cloud;
  PeerIdentity peer_identity{};
  PeerSettings peer_settings{};
  std::vector<StreamDefinition> streams{};
};

template <>
struct Formatter<ClientConfig> {
  template <typename TStream>
  void Format(ClientConfig const& value, FormatContext<TStream>& ctx) const {
    FormatTo(
        ctx.out(),
        FormatScheme{
            "parent_uid={},\nuid={},\nephemeral_uid={}\nmaster_key_type={},"
            "\ncloud=[\n{}\n]"},
        value.parent_uid, value.uid, value.ephemeral_uid,
        value.master_key.Index(), value.cloud);
  }
};
}  // namespace ae

#endif  // AETHER_TYPES_CLIENT_CONFIG_H_

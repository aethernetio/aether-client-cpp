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

#ifndef AETHER_CLIENT_CONNECTIONS_CLOUD_CACHE_H_
#define AETHER_CLIENT_CONNECTIONS_CLOUD_CACHE_H_

#include <map>
#include <optional>
#include <functional>

#include "aether/common.h"
#include "aether/uid.h"
#include "aether/cloud.h"

#include "aether/client_connections/server_connection_selector.h"

namespace ae {
/**
 * \brief client uid to cloud cache
 */
class CloudCache {
 public:
  CloudCache() = default;
  AE_CLASS_NO_COPY_MOVE(CloudCache)

  std::optional<std::reference_wrapper<Cloud::ptr>> GetCache(Uid uid);
  void AddCloud(Uid uid, Cloud::ptr cloud);

  AE_REFLECT_MEMBERS(clouds_)

 private:
  std::map<Uid, Cloud::ptr> clouds_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLOUD_CACHE_H_

/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_WORK_CLOUD_API_CLOUD_CONFIGS_H_
#define AETHER_WORK_CLOUD_API_CLOUD_CONFIGS_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "aether/types/uid.h"
#include "aether/types/server_id.h"

namespace ae {
struct CloudDescriptor {
  AE_REFLECT_MEMBERS(sids)

  std::vector<ServerId> sids;
};

struct UidAndCloudDescriptor {
  AE_REFLECT_MEMBERS(uid, cloud)
  Uid uid;
  CloudDescriptor cloud;
};

struct CloudConfig {
  AE_REFLECT_MEMBERS(subject_uid, config_version, cloud)

  Uid subject_uid;
  std::int64_t config_version;
  CloudDescriptor cloud;
};

struct AppliedConfig {
  AE_REFLECT_MEMBERS(subject_uid, config_version)

  Uid subject_uid;
  std::int64_t config_version;
};

}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_CLOUD_CONFIGS_H_

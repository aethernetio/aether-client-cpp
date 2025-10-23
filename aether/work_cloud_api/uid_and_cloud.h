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

#ifndef AETHER_WORK_CLOUD_API_UID_AND_CLOUD_H_
#define AETHER_WORK_CLOUD_API_UID_AND_CLOUD_H_

#include <vector>

#include "aether/types/uid.h"
#include "aether/common.h"

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
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_UID_AND_CLOUD_H_

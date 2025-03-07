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

#ifndef AETHER_METHODS_UID_AND_CLOUD_H_
#define AETHER_METHODS_UID_AND_CLOUD_H_

#include <vector>

#include "aether/uid.h"
#include "aether/common.h"

namespace ae {
struct UidAndCloud {
  AE_REFLECT_MEMBERS(uid, cloud)
  Uid uid;
  std::vector<ServerId> cloud;
};
}  // namespace ae

#endif  // AETHER_METHODS_UID_AND_CLOUD_H_

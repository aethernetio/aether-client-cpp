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

#ifndef AETHER_WORK_CLOUD_H_
#define AETHER_WORK_CLOUD_H_

#include "aether/obj/obj.h"
#include "aether/cloud.h"

namespace ae {
class Aether;
class WorkCloud : public Cloud {
  AE_OBJECT(WorkCloud, Cloud, 0)

  WorkCloud() = default;

 public:
#ifdef AE_DISTILLATION
  explicit WorkCloud(Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT()
};
}  // namespace ae
#endif  // AETHER_WORK_CLOUD_H_

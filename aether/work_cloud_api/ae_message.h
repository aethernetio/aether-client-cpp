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

#ifndef AETHER_WORK_CLOUD_API_AE_MESSAGE_H_
#define AETHER_WORK_CLOUD_API_AE_MESSAGE_H_

#include "aether/types/uid.h"
#include "aether/reflect/reflect.h"
#include "aether/types/data_buffer.h"

namespace ae {
struct AeMessage {
  AE_REFLECT_MEMBERS(uid, data)

  Uid uid;
  DataBuffer data;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_AE_MESSAGE_H_

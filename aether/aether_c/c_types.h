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

#ifndef AETHER_AETHER_C_C_TYPES_H_
#define AETHER_AETHER_C_C_TYPES_H_

#include "aether/aether_c/extern_c.h"

AE_EXTERN_C_BEGIN
typedef enum ActionStatus {
  kSuccess,
  kFailure,
  kStopped,
} ActionStatus;

typedef void* WriteActionHandle;

typedef void (*ActionStatusCb)(ActionStatus status, void* user_data);

AE_EXTERN_C_END

#endif  // AETHER_AETHER_C_C_TYPES_H_

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

#ifndef AETHER_AETHER_TELE_H_
#define AETHER_AETHER_TELE_H_

#include "aether/tele/tele.h"

AE_TELE_MODULE(kAether, 2, 3, 5);
AE_TAG(AetherStarted, kAether)
AE_TAG(AetherCreated, kAether)
AE_TAG(AetherDestroyed, kAether)

#endif  // AETHER_AETHER_TELE_H_

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

#ifndef EXAMPLES_COMMON_AETHER_CONSTRUCT_H_
#define EXAMPLES_COMMON_AETHER_CONSTRUCT_H_

#ifndef AE_EXAMPLE_ETHERNET
#  define AE_EXAMPLE_ETHERNET 0
#endif
#ifndef AE_EXAMPLE_ESP_WIFI
#  define AE_EXAMPLE_ESP_WIFI 0
#endif
#ifndef AE_EXAMPLE_LORA_MODULE
#  define AE_EXAMPLE_LORA_MODULE 0
#endif
#ifndef AE_EXAMPLE_MODEM
#  define AE_EXAMPLE_MODEM 0
#endif

#include <memory>

#include "aether/all.h"

namespace ae::examples {
static std::unique_ptr<AetherApp> construct_aether_app();
}

#endif  // EXAMPLES_COMMON_AETHER_CONSTRUCT_H_

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

#ifndef AETHER_ADAPTERS_ADAPTER_FACTORY_H_
#define AETHER_ADAPTERS_ADAPTER_FACTORY_H_

#include "aether/config.h"
#if defined AE_DISTILLATION

#  include "aether/ptr/ptr.h"
#  include "aether/obj/domain.h"
#  include "aether/aether.h"
#  include "aether/adapters/adapter.h"

namespace ae {
class AdapterFactory {
 public:
  static Adapter::ptr Create(Domain* domain, Aether::ptr const& aether);
};
}  // namespace ae
#endif

#endif  // AETHER_ADAPTERS_ADAPTER_FACTORY_H_

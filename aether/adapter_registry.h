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

#ifndef AETHER_ADAPTER_REGISTRY_H_
#define AETHER_ADAPTER_REGISTRY_H_

#include "aether/obj/obj.h"

#include "aether/adapters/adapter.h"

namespace ae {
class AdapterRegistry final : public Obj {
  AE_OBJECT(AdapterRegistry, Obj, 0)

  AdapterRegistry() = default;

 public:
#if AE_DISTILLATION
  explicit AdapterRegistry(Domain* domain);
#endif

  AE_OBJECT_REFLECT(AE_MMBRS(adapters_))

  /**
   * \brief Add adapter to the registry
   */
  void Add(Adapter::ptr adapter);

  std::vector<Adapter::ptr> const& adapters() const;

 private:
  std::vector<Adapter::ptr> adapters_;
};
}  // namespace ae

#endif  // AETHER_ADAPTER_REGISTRY_H_

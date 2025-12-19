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

 public:
  explicit AdapterRegistry(Domain* domain);

  /**
   * \brief Add adapter to the registry
   */
  void Add(std::shared_ptr<Adapter> adapter);

  std::vector<std::shared_ptr<Adapter>> const& adapters() const;

 private:
  std::vector<std::shared_ptr<Adapter>> adapters_;
};
}  // namespace ae

#endif  // AETHER_ADAPTER_REGISTRY_H_

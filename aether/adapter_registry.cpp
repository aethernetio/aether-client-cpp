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

#include "aether/adapter_registry.h"

namespace ae {
#if AE_DISTILLATION
AdapterRegistry::AdapterRegistry(Domain* domain) : Obj{domain} {}
#endif

void AdapterRegistry::Add(Adapter::ptr adapter) {
  adapters_.emplace_back(std::move(adapter));
}

std::vector<Channel::ptr> AdapterRegistry::GenerateChannels(
    std::vector<UnifiedAddress> const& server_channels) {
  std::vector<Channel::ptr> res;
  for (auto& adapter : adapters_) {
    if (!adapter) {
      domain_->LoadRoot(adapter);
    }
    auto channels = adapter->GenerateChannels(server_channels);
    res.insert(res.end(), channels.begin(), channels.end());
  }
  return res;
}

std::vector<Adapter::ptr> const& AdapterRegistry::adapters() const {
  return adapters_;
}
}  // namespace ae

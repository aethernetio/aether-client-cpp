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

#include "aether/adapters/adapter.h"

#include <algorithm>

#include "aether/global_ids.h"

namespace ae {

#ifdef AE_DISTILLATION
Adapter::Adapter(Domain* domain) : Obj{domain} {
  proxy_prefab_ = domain->CreateObj<Proxy>(GlobalId::kProxyFactory);
  proxy_prefab_.SetFlags(ae::ObjFlags::kUnloadedByDefault);
}
#endif  // AE_DISTILLATION

void Adapter::CleanDeadTransports() {
#if 0
  std::vector<decltype(transports_cache_)::iterator> to_erase;
  for (auto it = std::begin(transports_cache_);
       it != std::end(transports_cache_); ++it) {
    if (!static_cast<bool>(it->second)) {
      to_erase.push_back(it);
    }
  }

  for (auto erase_it : to_erase) {
    transports_cache_.erase(erase_it);
  }
#endif
}

std::unique_ptr<ITransport> Adapter::FindInCache(
    IpAddressPortProtocol /* address_port_protocol */) {
#if 0  // transport cache for different clients is not working
  auto cache_it = transports_cache_.find(address_port_protocol);
  if (cache_it != transports_cache_.end()) {
    auto transport = cache_it->second.Lock();
    if (!transport) {
      transports_cache_.erase(cache_it);
    } else {
      return transport;
    }
  }
#endif
  return {};
}

void Adapter::AddToCache(IpAddressPortProtocol /* address_port_protocol */,
                         ITransport& /* transport */) {
#if 0
  transports_cache_.emplace(address_port_protocol, std::move(transport));
#endif
}

}  // namespace ae

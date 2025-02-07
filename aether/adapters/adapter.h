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

#ifndef AETHER_ADAPTERS_ADAPTER_H_
#define AETHER_ADAPTERS_ADAPTER_H_

#include <map>
#include <vector>

#include "aether/obj/obj.h"
#include "aether/ptr/ptr.h"
#include "aether/actions/action.h"
#include "aether/ptr/ptr_view.h"
#include "aether/adapters/proxy.h"

#include "aether/transport/itransport.h"

namespace ae {
class CreateTransportAction : public Action<CreateTransportAction> {
 public:
  using Action::Action;
  using Action::operator=;

  virtual Ptr<ITransport> transport() const = 0;
};

// TODO: make it pure virtual

class Adapter : public Obj {
  AE_OBJECT(Adapter, Obj, 0)

 protected:
  Adapter() = default;

 public:
#ifdef AE_DISTILLATION
  explicit Adapter(Domain* domain);
#endif  // AE_DISTILLATION

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(proxies_, proxy_prefab_);
  }

  virtual ActionView<CreateTransportAction> CreateTransport(
      IpAddressPortProtocol const& /* address_port_protocol */) {
    return {};
  }

  virtual IpAddress ip_address() const { return {}; }

 protected:
  void CleanDeadTransports();
  Ptr<ITransport> FindInCache(IpAddressPortProtocol address_port_protocol);
  void AddToCache(IpAddressPortProtocol address_port_protocol,
                  Ptr<ITransport> transport);

  Proxy::ptr proxy_prefab_;
  std::vector<Proxy::ptr> proxies_;

  std::map<IpAddressPortProtocol, PtrView<ITransport>> transports_cache_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_ADAPTER_H_ */

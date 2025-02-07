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

#ifndef AETHER_ADAPTERS_ETHERNET_H_
#define AETHER_ADAPTERS_ETHERNET_H_

#include "aether/ptr/ptr.h"
#include "aether/poller/poller.h"
#include "aether/adapters/adapter.h"
#include "aether/actions/action_list.h"

namespace ae {
class Aether;

class EthernetAdapter : public Adapter {
  AE_OBJECT(EthernetAdapter, Adapter, 0)

  EthernetAdapter() = default;

  class EthernetCreateTransportAction : public CreateTransportAction {
   public:
    explicit EthernetCreateTransportAction(ActionContext action_context,
                                           Ptr<ITransport> transport);

    TimePoint Update(TimePoint current_time) override;

    Ptr<ITransport> transport() const override;

   private:
    Ptr<ITransport> transport_;
    bool once_;
  };

 public:
#ifdef AE_DISTILLATION
  EthernetAdapter(ObjPtr<Aether> aether, IPoller::ptr poller, Domain* domain);
#endif  // AE_DISTILLATION

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(aether_, poller_);
  }

  ActionView<CreateTransportAction> CreateTransport(
      IpAddressPortProtocol const& address_port_protocol) override;

 private:
  Obj::ptr aether_;
  IPoller::ptr poller_;
  Ptr<ActionList<EthernetCreateTransportAction>> create_transport_actions_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_ETHERNET_H_

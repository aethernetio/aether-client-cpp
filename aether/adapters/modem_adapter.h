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

#ifndef AETHER_ADAPTERS_MODEM_ADAPTER_H_
#define AETHER_ADAPTERS_MODEM_ADAPTER_H_

#  define MODEM_ADAPTER_ENABLED 1

#include <cstdint>
#include <string>

#include "aether/adapters/parent_modem.h"
#include "aether/adapters/modems/imodem_driver.h"

#include "aether/events/events.h"
#include "aether/actions/action_list.h"
#include "aether/events/event_subscription.h"


namespace ae {

class ModemAdapter : public ParentModemAdapter {
  AE_OBJECT(ModemAdapter, ParentModemAdapter, 0)

  ModemAdapter() = default;

  class CreateTransportAction : public ae::CreateTransportAction {
   public:
    // immediately create the transport
    CreateTransportAction(ActionContext action_context,
                          ModemAdapter* adapter, Obj::ptr const& aether,
                          IPoller::ptr const& poller,
                          IpAddressPortProtocol address_port_protocol_);
    // create the transport when wifi is connected
    CreateTransportAction(ActionContext action_context,
                          EventSubscriber<void(bool)> modem_connected_event,
                          ModemAdapter* adapter, Obj::ptr const& aether,
                          IPoller::ptr const& poller,
                          IpAddressPortProtocol address_port_protocol_);
                          
    ActionResult Update() override;

    std::unique_ptr<ITransport> transport() override;

   private:
    void CreateTransport();
    
    ModemAdapter* adapter_;
    PtrView<Aether> aether_;
    PtrView<IPoller> poller_;
    IpAddressPortProtocol address_port_protocol_;

    bool once_;
    bool failed_;
    Subscription modem_connected_subscription_;
    std::unique_ptr<ITransport> transport_;
  };

 public:
#ifdef AE_DISTILLATION
  ModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller, ModemInit modem_init,
               Domain* domain);
  ~ModemAdapter();
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(create_transport_actions_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
  }

  ActionView<ae::CreateTransportAction> CreateTransport(
      IpAddressPortProtocol const& address_port_protocol) override;

  void Update(TimePoint p) override;

 private:
  void Connect(void);
  void DisConnect(void);

  bool connected_{false};
  Event<void(bool result)> modem_connected_event_;
  std::unique_ptr<IModemDriver> modem_driver_;
  std::optional<ActionList<CreateTransportAction>> create_transport_actions_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_MODEM_ADAPTER_H_
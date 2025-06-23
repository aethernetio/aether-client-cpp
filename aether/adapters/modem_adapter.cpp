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

#include "aether/adapters/modem_adapter.h"

#include "aether/adapters/modems/modem_factory.h"
#include "aether/transport/low_level/tcp/lte_tcp.h"

#include "aether/adapters/adapter_tele.h"

namespace ae {

ModemAdapter::CreateTransportAction::CreateTransportAction(
    ActionContext action_context, ModemAdapter* adapter,
    Obj::ptr const& aether, IPoller::ptr const& poller,
    IpAddressPortProtocol address_port_protocol)
    : ae::CreateTransportAction{action_context},
      adapter_{adapter},
      aether_{Ptr<Aether>(aether)},
      poller_{poller},
      address_port_protocol_{std::move(address_port_protocol)},
      once_{true},
      failed_{false} {
  AE_TELE_DEBUG(kAdapterModemTransportImmediately,
                "Modem connected, create transport immediately");
  CreateTransport();
}

ModemAdapter::CreateTransportAction::CreateTransportAction(
    ActionContext action_context,
    EventSubscriber<void(bool)> modem_connected_event, ModemAdapter* adapter,
    Obj::ptr const& aether, IPoller::ptr const& poller,
    IpAddressPortProtocol address_port_protocol)
    : ae::CreateTransportAction{action_context},
      adapter_{adapter},
      aether_{Ptr<Aether>(aether)},
      poller_{poller},
      address_port_protocol_{std::move(address_port_protocol)},
      once_{true},
      failed_{false},
      modem_connected_subscription_{
          modem_connected_event.Subscribe([this](auto result) {
            if (result) {
              CreateTransport();
            } else {
              failed_ = true;
            }
            Action::Trigger();
          })} {
  AE_TELE_DEBUG(kAdapterModemTransportWait, "Wait modem connection");
}

ActionResult ModemAdapter::CreateTransportAction::Update() {
  if (transport_ && once_) {    
    once_ = false;
    return ActionResult::Result();
  } else if (failed_ && once_) {    
    once_ = false;
    return ActionResult::Error();
  }
  return {};
}

std::unique_ptr<ITransport>
ModemAdapter::CreateTransportAction::transport() {
  return std::move(transport_);
}

void ModemAdapter::CreateTransportAction::CreateTransport() {
  adapter_->CleanDeadTransports();
  transport_ = adapter_->FindInCache(address_port_protocol_);
  if (!transport_) {
    AE_TELE_DEBUG(kAdapterCreateCacheMiss);
#  if defined(LTE_TCP_TRANSPORT_ENABLED)
    assert(address_port_protocol_.protocol == Protocol::kTcp);
    transport_ =
        make_unique<LteTcpTransport>(*aether_.Lock()->action_processor,
                                      poller_.Lock(), address_port_protocol_);
#  else
    return {};
#  endif
  } else {
    AE_TELE_DEBUG(kAdapterCreateCacheHit);
  }

  adapter_->AddToCache(address_port_protocol_, *transport_);
}

#  if defined AE_DISTILLATION
ModemAdapter::ModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                   ModemInit modem_init,
                                   Domain* domain)
    : ParentModemAdapter{std::move(aether), std::move(poller), std::move(modem_init),
                        domain} {
  modem_driver_ = ModemDriverFactory::CreateModem(modem_init_);
  AE_TELED_DEBUG("Modem instance created!");
  
}

ModemAdapter::~ModemAdapter() {
  if (connected_ == true) {
    DisConnect();
    AE_TELE_DEBUG(kAdapterDestructor, "Modem instance deleted!");
    connected_ = false;
  }
}
#  endif  // AE_DISTILLATION

ActionView<ae::CreateTransportAction> ModemAdapter::CreateTransport(
    IpAddressPortProtocol const& address_port_protocol) {
  AE_TELE_INFO(kAdapterCreate, "Create transport for {}",
               address_port_protocol);  
  if (!create_transport_actions_) {
    create_transport_actions_.emplace(
        ActionContext{*aether_.as<Aether>()->action_processor});
  }
  if (connected_) {
    return create_transport_actions_->Emplace(this, aether_, poller_,
                                              address_port_protocol);
  } else {
    return create_transport_actions_->Emplace(
        EventSubscriber{modem_connected_event_}, this, aether_, poller_,
        address_port_protocol);
  }
}

void ModemAdapter::Update(TimePoint t) {
  if (connected_ == false) {
    connected_ = true;
    Connect();
  }

  update_time_ = t;
}

void ModemAdapter::Connect(void) {  
  AE_TELE_DEBUG(kAdapterModemConnected, "Modem connecting to the AP");
  modem_driver_->Init();
}

void ModemAdapter::DisConnect(void) {  
  AE_TELE_DEBUG(kAdapterModemDisconnected, "Modem disconnecting from the AP");
  modem_driver_->Stop();
}

}  // namespace ae
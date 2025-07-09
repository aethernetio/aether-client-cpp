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

namespace modem_adapter_internal {
class ModemAdapterTransportBuilder final : public ITransportBuilder {
 public:
  ModemAdapterTransportBuilder(ModemAdapter& adapter,
                               IpAddressPortProtocol address_port_protocol)
      : adapter_{&adapter},
        address_port_protocol_{std::move(address_port_protocol)} {}

  std::unique_ptr<ITransport> BuildTransport() override {
#  if defined(LWIP_TCP_TRANSPORT_ENABLED)
    assert(address_port_protocol_.protocol == Protocol::kTcp);
    return make_unique<LteTcpTransport>(*adapter_->aether_.as<Aether>(),
                                         adapter_->poller_,
                                         address_port_protocol_);
#  else
    return {};
#  endif
  }

 private:
  ModemAdapter* adapter_;
  IpAddressPortProtocol address_port_protocol_;
};

ModemAdapterTransportBuilderAction::ModemAdapterTransportBuilderAction(
    ActionContext action_context, ModemAdapter& adapter,
    UnifiedAddress address_port_protocol)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_port_protocol_{std::move(address_port_protocol)},
      state_{State::kAddressResolve},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kAdapterModemTransportImmediately,
                "LTE modem connected, create transport immediately");
}

ModemAdapterTransportBuilderAction::ModemAdapterTransportBuilderAction(
    ActionContext action_context,
    EventSubscriber<void(bool)> lte_modem_connected_event, ModemAdapter& adapter,
    UnifiedAddress address_port_protocol)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_port_protocol_{std::move(address_port_protocol)},
      state_{State::kWaitConnection},
      lte_modem_connected_subscription_{
          lte_modem_connected_event.Subscribe([this](auto result) {
            if (result) {
              state_ = State::kAddressResolve;
            } else {
              state_ = State::kFailed;
            }
          })} {
  AE_TELE_DEBUG(kAdapterModemTransportWait, "Wait LTE modem connection");
}

ActionResult ModemAdapterTransportBuilderAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kAddressResolve:
        ResolveAddress();
        break;
      case State::kBuildersCreate:
        CreateBuilders();
        break;
      case State::kBuildersCreated:
        return ActionResult::Result();
      case State::kFailed:
        return ActionResult::Error();
    }
  }
  return {};
}

std::vector<std::unique_ptr<ITransportBuilder>>
ModemAdapterTransportBuilderAction::builders() {
  return std::move(transport_builders_);
}

#  if AE_SUPPORT_CLOUD_DNS
void ModemAdapterTransportBuilderAction::ResolveAddress() {
  std::visit(
      reflect::OverrideFunc{
          [this](IpAddressPortProtocol const& ip_address) {
            ip_address_port_protocols_.push_back(ip_address);
            state_ = State::kBuildersCreate;
          },
          [this](NameAddress const& name_address) {
            auto dns_resolver = adapter_->dns_resolver_;
            assert(dns_resolver);
            auto resolve_action = dns_resolver->Resolve(name_address);

            address_resolved_ =
                resolve_action->ResultEvent().Subscribe([this](auto& action) {
                  ip_address_port_protocols_ = std::move(action.addresses);
                  state_ = State::kBuildersCreate;
                });
            resolving_failed_ = resolve_action->ErrorEvent().Subscribe(
                [this](auto&) { state_ = State::kFailed; });
          }},
      address_port_protocol_);
}
#  else
void ModemAdapterTransportBuilderAction::ResolveAddress() {
  auto ip_address = std::get<IpAddressPortProtocol>(address_port_protocol_);
  ip_address_port_protocols_.push_back(ip_address);
  state_ = State::kBuildersCreate;
}
#  endif

void ModemAdapterTransportBuilderAction::CreateBuilders() {
  for (auto const& ip_address_port_protocol : ip_address_port_protocols_) {
    auto builder = std::make_unique<ModemAdapterTransportBuilder>(
        *adapter_, ip_address_port_protocol);
    transport_builders_.push_back(std::move(builder));
  }
  state_ = State::kBuildersCreated;
}
}  // namespace modem_adapter_internal

#  if defined AE_DISTILLATION
ModemAdapter::ModemAdapter(ObjPtr<Aether> aether, IPoller::ptr poller, DnsResolver::ptr dns_resolver, 
                                   ModemInit modem_init,
                                   Domain* domain)
    : ParentModemAdapter{std::move(aether), std::move(poller), std::move(dns_resolver), std::move(modem_init),
                        domain} {
  modem_driver_ = ModemDriverFactory::CreateModem(modem_init_);
  AE_TELED_DEBUG("Modem instance created!");
  
}

ModemAdapter::~ModemAdapter() {
  if (connected_ == true) {
    DisConnect();
    AE_TELED_DEBUG("Modem instance deleted!");
    connected_ = false;
  }
}
#  endif  // AE_DISTILLATION

ActionView<TransportBuilderAction> ModemAdapter::CreateTransport(
    UnifiedAddress const& address_port_protocol) {
  AE_TELE_INFO(kAdapterModemAdapterCreate, "Create transport for {}",
               address_port_protocol);
  if (!transport_builders_actions_) {
    transport_builders_actions_.emplace(ActionContext{*aether_.as<Aether>()});
  }
  if (connected_) {
    return transport_builders_actions_->Emplace(*this, address_port_protocol);
  } else {
    return transport_builders_actions_->Emplace(
        EventSubscriber{modem_connected_event_}, *this, address_port_protocol);
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
  std::vector<std::uint8_t> data{'H','e','l','l','o',' ','w','o','r','l','d','!'};
  std::size_t size{};
  
  AE_TELE_DEBUG(kAdapterModemConnected, "Modem connecting to the AP");
  modem_driver_->Init();
  modem_driver_->Setup();
  modem_driver_->OpenNetwork(0, 0, ae::Protocol::kTcp, "dbservice.aethernet.io", 8889);
  modem_driver_->WritePacket(0, data);
  modem_driver_->ReadPacket(0, data, size);
  modem_driver_->CloseNetwork(0, 0);
}

void ModemAdapter::DisConnect(void) {  
  AE_TELE_DEBUG(kAdapterModemDisconnected, "Modem disconnecting from the AP");
  modem_driver_->Stop();
}

}  // namespace ae
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

#include "aether/modems/modem_factory.h"
#include "aether/transport/modems/modem_transport.h"
#include "aether/transport/itransport_stream_builder.h"

#include "aether/adapters/adapter_tele.h"

namespace ae {
namespace modem_adapter_internal {
class ModemAdapterTransportBuilder final : public ITransportStreamBuilder {
 public:
  ModemAdapterTransportBuilder(ModemAdapter& adapter, UnifiedAddress address)
      : adapter_{&adapter}, address_{std::move(address)} {}

  std::unique_ptr<ByteIStream> BuildTransportStream() override {
    [[maybe_unused]] auto protocol = std::visit(
        [&](auto const& address_port_protocol) {
          return address_port_protocol.protocol;
        },
        address_);
    assert(protocol == Protocol::kTcp || protocol == Protocol::kUdp);
#if defined MODEM_TRANSPORT_ENABLED
    return make_unique<ModemTransport>(*adapter_->aether_.as<Aether>(),
                                       *adapter_->modem_driver_, address_);
#else
    return nullptr;
#endif
  }

 private:
  ModemAdapter* adapter_;
  UnifiedAddress address_;
};

ModemAdapterTransportBuilderAction::ModemAdapterTransportBuilderAction(
    ActionContext action_context, ModemAdapter& adapter,
    UnifiedAddress address_)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_{std::move(address_)},
      state_{State::kBuildersCreate},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kAdapterModemTransportImmediately,
                "LTE modem connected, create transport immediately");
}

ModemAdapterTransportBuilderAction::ModemAdapterTransportBuilderAction(
    ActionContext action_context,
    EventSubscriber<void(bool)> lte_modem_connected_event,
    ModemAdapter& adapter, UnifiedAddress address_)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_{std::move(address_)},
      state_{State::kWaitConnection},
      lte_modem_connected_subscription_{
          lte_modem_connected_event.Subscribe([this](auto result) {
            if (result) {
              state_ = State::kBuildersCreate;
            } else {
              state_ = State::kFailed;
            }
          })} {
  AE_TELE_DEBUG(kAdapterModemTransportWait, "Wait LTE modem connection");
}

UpdateStatus ModemAdapterTransportBuilderAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kBuildersCreate:
        CreateBuilders();
        break;
      case State::kBuildersCreated:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  return {};
}

std::vector<std::unique_ptr<ITransportStreamBuilder>>
ModemAdapterTransportBuilderAction::builders() {
  std::vector<std::unique_ptr<ITransportStreamBuilder>> res;
  res.emplace_back(std::move(transport_builder_));
  return res;
}

void ModemAdapterTransportBuilderAction::CreateBuilders() {
  transport_builder_ =
      std::make_unique<ModemAdapterTransportBuilder>(*adapter_, address_);
  state_ = State::kBuildersCreated;
}
}  // namespace modem_adapter_internal

#if defined AE_DISTILLATION
ModemAdapter::ModemAdapter(ObjPtr<Aether> aether, ModemInit modem_init,
                           Domain* domain)
    : ParentModemAdapter{std::move(aether), std::move(modem_init), domain} {
  modem_driver_ = ModemDriverFactory::CreateModem(modem_init_, domain);
  modem_driver_->Init();
  AE_TELED_DEBUG("Modem instance created!");
}
#endif  // AE_DISTILLATION

ModemAdapter::~ModemAdapter() {
  if (connected_) {
    DisConnect();
    AE_TELED_DEBUG("Modem instance deleted!");
    connected_ = false;
  }
}

ActionPtr<TransportBuilderAction> ModemAdapter::CreateTransport(
    UnifiedAddress const& address_port_protocol) {
  AE_TELE_INFO(kAdapterModemAdapterCreate, "Create transport for {}",
               address_port_protocol);
  if (connected_) {
    return ActionPtr<
        modem_adapter_internal::ModemAdapterTransportBuilderAction>(
        *aether_.as<Aether>(), *this, address_port_protocol);
  }
  return ActionPtr<modem_adapter_internal::ModemAdapterTransportBuilderAction>(
      *aether_.as<Aether>(), EventSubscriber{modem_connected_event_}, *this,
      address_port_protocol);
}

std::vector<ObjPtr<Channel>> ModemAdapter::GenerateChannels(
    std::vector<UnifiedAddress> const& addresses) {
  std::vector<ObjPtr<Channel>> channels;
  auto self_ptr = ObjPtr{MakePtrFromThis(this)};
  for (auto const& address : addresses) {
    channels.emplace_back(domain_->CreateObj<Channel>(self_ptr));
    channels.back()->address = address;
  }
  return channels;
}

void ModemAdapter::Update(TimePoint current_time) {
  if (!connected_) {
    connected_ = true;
    Connect();
  }

  update_time_ = current_time;
}

void ModemAdapter::Connect() {
  AE_TELE_DEBUG(kAdapterModemDisconnected, "Modem connecting to the network");
  if (!modem_driver_->Start()) {
    AE_TELED_ERROR("Modem driver does not start");
    return;
  }
  modem_connected_event_.Emit(true);
}

void ModemAdapter::DisConnect() {
  AE_TELE_DEBUG(kAdapterModemDisconnected,
                "Modem disconnecting from the network");
  if (!modem_driver_->Stop()) {
    AE_TELED_ERROR("Modem driver does not stop");
    return;
  }
  modem_connected_event_.Emit(false);
}

}  // namespace ae

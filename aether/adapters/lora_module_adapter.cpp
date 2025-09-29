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

#include "aether/adapters/lora_module_adapter.h"

#include "aether/lora_modules/lora_module_factory.h"
#include "aether/transport/lora_modules/lora_module_transport.h"
#include "aether/transport/itransport_stream_builder.h"

#include "aether/adapters/adapter_tele.h"

namespace ae {
namespace lora_module_adapter_internal {
class LoraModuleAdapterTransportBuilder final : public ITransportStreamBuilder {
 public:
  LoraModuleAdapterTransportBuilder(LoraModuleAdapter& adapter,
                                    UnifiedAddress address)
      : adapter_{&adapter}, address_{std::move(address)} {}

  std::unique_ptr<ByteIStream> BuildTransportStream() override {
    [[maybe_unused]] auto protocol = std::visit(
        [&](auto const& address_port_protocol) {
          return address_port_protocol.protocol;
        },
        address_);
    assert(protocol == Protocol::kTcp || protocol == Protocol::kUdp);
#if defined LORA_MODULE_TRANSPORT_ENABLED
    return make_unique<LoraModuleTransport>(
        *adapter_->aether_.as<Aether>(),
        *adapter_->lora_module_driver_, address_);
#else
    return nullptr;
#endif
  }

 private:
  LoraModuleAdapter* adapter_;
  UnifiedAddress address_;
};

LoraModuleAdapterTransportBuilderAction::
    LoraModuleAdapterTransportBuilderAction(ActionContext action_context,
                                            LoraModuleAdapter& adapter,
                                            UnifiedAddress address_)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_{std::move(address_)},
      state_{State::kBuildersCreate},
      state_changed_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELE_DEBUG(kAdapterLoraModuleTransportImmediately,
                "Lora module connected, create transport immediately");
}

LoraModuleAdapterTransportBuilderAction::
    LoraModuleAdapterTransportBuilderAction(
        ActionContext action_context,
        EventSubscriber<void(bool)> lora_module_connected_event,
        LoraModuleAdapter& adapter, UnifiedAddress address_)
    : TransportBuilderAction{action_context},
      adapter_{&adapter},
      address_{std::move(address_)},
      state_{State::kWaitConnection},
      lora_module_connected_subscription_{
          lora_module_connected_event.Subscribe([this](auto result) {
            if (result) {
              state_ = State::kBuildersCreate;
            } else {
              state_ = State::kFailed;
            }
          })} {
  AE_TELE_DEBUG(kAdapterLoraModuleTransportWait, "Wait Lora module connection");
}

UpdateStatus LoraModuleAdapterTransportBuilderAction::Update() {
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
LoraModuleAdapterTransportBuilderAction::builders() {
  std::vector<std::unique_ptr<ITransportStreamBuilder>> res;
  res.emplace_back(std::move(transport_builder_));
  return res;
}

void LoraModuleAdapterTransportBuilderAction::CreateBuilders() {
  transport_builder_ =
      std::make_unique<LoraModuleAdapterTransportBuilder>(*adapter_, address_);
  state_ = State::kBuildersCreated;
}
}  // namespace lora_module_adapter_internal

#if defined AE_DISTILLATION
LoraModuleAdapter::LoraModuleAdapter(ObjPtr<Aether> aether, 
                                     IPoller::ptr poller,
                                     LoraModuleInit lora_module_init,
                                     Domain* domain)
    : ParentLoraModuleAdapter{std::move(aether), poller,
                              lora_module_init, domain} {
  lora_module_driver_ =
      LoraModuleDriverFactory::CreateLoraModule(*this, poller, lora_module_init, domain);
  lora_module_driver_->Init();
  AE_TELED_DEBUG("Lora module instance created!");
}
#endif  // AE_DISTILLATION

LoraModuleAdapter::~LoraModuleAdapter() {
  if (connected_) {
    DisConnect();
    AE_TELED_DEBUG("Lora module instance deleted!");
    connected_ = false;
  }
}

ActionPtr<TransportBuilderAction> LoraModuleAdapter::CreateTransport(
    UnifiedAddress const& address_port_protocol) {
  AE_TELE_INFO(kAdapterLoraModuleAdapterCreate, "Create transport for {}",
               address_port_protocol);
  if (connected_) {
    return ActionPtr<
        lora_module_adapter_internal::LoraModuleAdapterTransportBuilderAction>(
        *aether_.as<Aether>(), *this, address_port_protocol);
  }
  return ActionPtr<
      lora_module_adapter_internal::LoraModuleAdapterTransportBuilderAction>(
      *aether_.as<Aether>(), EventSubscriber{lora_module_connected_event_},
      *this, address_port_protocol);
}

void LoraModuleAdapter::Update(TimePoint current_time) {
  if (!connected_) {
    connected_ = true;
    Connect();
  }

  update_time_ = current_time;
}

void LoraModuleAdapter::Connect() {
  AE_TELE_DEBUG(kAdapterLoraModuleDisconnected,
                "Lora module connecting to the network");
  if (!lora_module_driver_->Start()) {
    AE_TELED_ERROR("Lora module driver does not start");
    return;
  }
  lora_module_connected_event_.Emit(true);
}

void LoraModuleAdapter::DisConnect() {
  AE_TELE_DEBUG(kAdapterLoraModuleDisconnected,
                "Lora module disconnecting from the network");
  if (!lora_module_driver_->Stop()) {
    AE_TELED_ERROR("Lora module driver does not stop");
    return;
  }
  lora_module_connected_event_.Emit(false);
}
}  // namespace ae

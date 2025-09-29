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

#ifndef AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_
#define AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_

#define LORA_MODULE_ADAPTER_ENABLED 1

#include <cstdint>

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"

#include "aether/lora_modules/ilora_module_driver.h"
#include "aether/adapters/parent_lora_module.h"

#define LORA_MODULE_TCP_TRANSPORT_ENABLED 1

namespace ae {
class LoraModuleAdapter;
namespace lora_module_adapter_internal {
class LoraModuleAdapterTransportBuilder;

class LoraModuleAdapterTransportBuilderAction final
    : public TransportBuilderAction {
  enum class State : std::uint8_t {
    kWaitConnection,
    kBuildersCreate,
    kBuildersCreated,
    kFailed
  };

 public:
  // immediately create the transport
  LoraModuleAdapterTransportBuilderAction(ActionContext action_context,
                                          LoraModuleAdapter& adapter,
                                          UnifiedAddress address_);
  // create the transport when lora module is connected
  LoraModuleAdapterTransportBuilderAction(
      ActionContext action_context,
      EventSubscriber<void(bool)> lora_module_connected_event,
      LoraModuleAdapter& adapter, UnifiedAddress address_);

  UpdateStatus Update() override;

  std::vector<std::unique_ptr<ITransportStreamBuilder>> builders() override;

 private:
  void CreateBuilders();

  LoraModuleAdapter* adapter_;
  UnifiedAddress address_;
  std::unique_ptr<ITransportStreamBuilder> transport_builder_;
  StateMachine<State> state_;
  Subscription state_changed_;
  Subscription lora_module_connected_subscription_;
  Subscription resolve_sub_;
};
}  // namespace lora_module_adapter_internal

class LoraModuleAdapter : public ParentLoraModuleAdapter {
  friend class lora_module_adapter_internal::LoraModuleAdapterTransportBuilder;

  AE_OBJECT(LoraModuleAdapter, ParentLoraModuleAdapter, 0)

  LoraModuleAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  LoraModuleAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                    LoraModuleInit lora_module_init, Domain* domain);
#endif  // AE_DISTILLATION

  ~LoraModuleAdapter() override;

  AE_OBJECT_REFLECT(AE_MMBRS(connected_, lora_module_connected_event_,
                             lora_module_driver_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
  }

  ActionPtr<TransportBuilderAction> CreateTransport(
      UnifiedAddress const& address_port_protocol) override;

  void Update(TimePoint current_time) override;

 private:
  void Connect();
  void DisConnect();

  bool connected_{false};
  Event<void(bool result)> lora_module_connected_event_;
  ILoraModuleDriver::ptr lora_module_driver_;
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_

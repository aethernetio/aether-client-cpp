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

#ifndef AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA
#  include "aether/obj/obj.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/types/state_machine.h"
#  include "aether/lora_modules/ilora_module_driver.h"
#  include "aether/adapters/lora_module_adapter.h"
#  include "aether/events/event_subscription.h"
#  include "aether/access_points/access_point.h"

namespace ae {
class Aether;

class LoraModuleConnectAction final : public Action<LoraModuleConnectAction> {
 public:
  enum class State : std::uint8_t {
    kStart,
    kSuccess,
    kFailed,
  };

  LoraModuleConnectAction(ActionContext action_context,
                          ILoraModuleDriver& driver);

  UpdateStatus Update();

 private:
  void Start();

  ILoraModuleDriver* driver_;
  Subscription start_sub_;
  StateMachine<State> state_;
};

class LoraModuleAccessPoint final : public AccessPoint {
  AE_OBJECT(LoraModuleAccessPoint, AccessPoint, 0)
  LoraModuleAccessPoint() = default;

 public:
  LoraModuleAccessPoint(ObjPtr<Aether> aether,
                        LoraModuleAdapter::ptr lora_module_adapter,
                        Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, lora_module_adapter_));

  ActionPtr<LoraModuleConnectAction> Connect();
  ILoraModuleDriver& lora_module_driver();

  std::vector<ObjPtr<Channel>> GenerateChannels(
      std::vector<Endpoint> const& endpoints) override;

 private:
  Obj::ptr aether_;
  LoraModuleAdapter::ptr lora_module_adapter_;
  ActionPtr<LoraModuleConnectAction> connect_action_;
  Subscription connect_sub_;
};
}  // namespace ae
#endif
#endif  // AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_

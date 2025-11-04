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

#include "aether/access_points/lora_module_access_point.h"
#if AE_SUPPORT_LORA

#  include "aether/aether.h"
#  include "aether/lora_modules/ilora_module_driver.h"

#  include "aether/channels/lora_module_channel.h"

namespace ae {
LoraModuleConnectAction::LoraModuleConnectAction(
    ActionContext action_context, [[maybe_unused]] ILoraModuleDriver& driver)
    : Action{action_context}, driver_{&driver}, state_{State::kStart} {
  AE_TELED_DEBUG("LoraModuleConnectAction created");
}

UpdateStatus LoraModuleConnectAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kStart:
        Start();
        break;
      case State::kSuccess:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  return {};
}

void LoraModuleConnectAction::Start() {
  AE_TELED_DEBUG("LoraModuleConnectAction start");
  auto action = driver_->Start();
  if (!action) {
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }
  action->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() {
        AE_TELED_INFO("Lora module access point start success");
        state_ = State::kSuccess;
        Action::Trigger();
      }},
      OnError{[this]() {
        AE_TELED_ERROR("Lora module access point start failed");
        state_ = State::kFailed;
        Action::Trigger();
      }},
  });
}

LoraModuleAccessPoint::LoraModuleAccessPoint(
    ObjPtr<Aether> aether, LoraModuleAdapter::ptr lora_module_adapter,
    Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      lora_module_adapter_{std::move(lora_module_adapter)} {}

ActionPtr<LoraModuleConnectAction> LoraModuleAccessPoint::Connect() {
  AE_TELED_DEBUG("Make lora module access point connection");

  // reuse connect action if it's in progress
  if (!connect_action_) {
    connect_action_ = ActionPtr<LoraModuleConnectAction>{
        *aether_.as<Aether>(), lora_module_adapter_->lora_module_driver()};
    connect_sub_ = connect_action_->FinishedEvent().Subscribe(
        [this]() { connect_action_.reset(); });
  }
  return connect_action_;
}

ILoraModuleDriver& LoraModuleAccessPoint::lora_module_driver() {
  return lora_module_adapter_->lora_module_driver();
}

std::vector<ObjPtr<Channel>> LoraModuleAccessPoint::GenerateChannels(
    std::vector<UnifiedAddress> const& endpoints) {
  AE_TELED_DEBUG("Generate lora module channels");
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(endpoints.size());
  Aether::ptr aether = aether_;
  LoraModuleAccessPoint::ptr self = MakePtrFromThis(this);
  for (auto const& endpoint : endpoints) {
    channels.emplace_back(
        domain_->CreateObj<LoraModuleChannel>(aether, self, endpoint));
  }
  return channels;
}

}  // namespace ae
#endif

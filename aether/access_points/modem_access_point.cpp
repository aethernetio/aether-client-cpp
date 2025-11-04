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

#include "aether/access_points/modem_access_point.h"
#if AE_SUPPORT_MODEMS

#  include "aether/aether.h"
#  include "aether/modems/imodem_driver.h"

#  include "aether/channels/modem_channel.h"

namespace ae {
ModemConnectAction::ModemConnectAction(ActionContext action_context,
                                       [[maybe_unused]] IModemDriver& driver)
    : Action{action_context}, driver_{&driver}, state_{State::kStart} {
  AE_TELED_DEBUG("ModemConnectAction created");
}

UpdateStatus ModemConnectAction::Update() {
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

void ModemConnectAction::Start() {
  AE_TELED_DEBUG("ModemConnectAction start");
  auto action = driver_->Start();
  if (!action) {
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }
  action->StatusEvent().Subscribe(ActionHandler{
      OnResult{[this]() {
        AE_TELED_INFO("Modem access point start success");
        state_ = State::kSuccess;
        Action::Trigger();
      }},
      OnError{[this]() {
        AE_TELED_ERROR("Modem access point start failed");
        state_ = State::kFailed;
        Action::Trigger();
      }},
  });
}

ModemAccessPoint::ModemAccessPoint(ObjPtr<Aether> aether,
                                   ModemAdapter::ptr modem_adapter,
                                   Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      modem_adapter_{std::move(modem_adapter)} {}

ActionPtr<ModemConnectAction> ModemAccessPoint::Connect() {
  AE_TELED_DEBUG("Make modem access point connection");

  // reuse connect action if it's in progress
  if (!connect_action_) {
    connect_action_ = ActionPtr<ModemConnectAction>{
        *aether_.as<Aether>(), modem_adapter_->modem_driver()};
    connect_sub_ = connect_action_->FinishedEvent().Subscribe(
        [this]() { connect_action_.reset(); });
  }
  return connect_action_;
}

IModemDriver& ModemAccessPoint::modem_driver() {
  return modem_adapter_->modem_driver();
}

std::vector<ObjPtr<Channel>> ModemAccessPoint::GenerateChannels(
    std::vector<UnifiedAddress> const& endpoints) {
  AE_TELED_DEBUG("Generate modem channels");
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(endpoints.size());
  Aether::ptr aether = aether_;
  ModemAccessPoint::ptr self = MakePtrFromThis(this);
  for (auto const& endpoint : endpoints) {
    channels.emplace_back(
        domain_->CreateObj<ModemChannel>(aether, self, endpoint));
  }
  return channels;
}

}  // namespace ae
#endif

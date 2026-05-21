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
#  include "aether/server.h"
#  include "aether/modems/imodem_driver.h"

#  include "aether/channels/modem_channel.h"
#  include "aether/access_points/filter_endpoints.h"

#  include "aether/tele/tele.h"

namespace ae {
ModemConnectAction::ModemConnectAction(AeContext const& ae_context,
                                       IModemDriver& driver)
    : driver_{&driver},
      task_sub_{ae_context.scheduler().Task([&]() noexcept { Start(); })} {
  AE_TELED_DEBUG("ModemConnectAction created");
}

ModemConnectAction::ConnectionEvent::Subscriber
ModemConnectAction::connection_event() {
  return EventSubscriber{connection_event_};
}

void ModemConnectAction::Start() {
  AE_TELED_DEBUG("ModemConnectAction start");
  auto* op = driver_->Start();
  if (op == nullptr) {
    connection_event_.Emit(false);
    Finish();
    return;
  }
  // check immediate result
  if (auto const& res = op->result(); res) {
    if (res->IsOk()) {
      AE_TELED_INFO("Modem access point start success");
      connection_event_.Emit(true);
    } else {
      AE_TELED_ERROR("Modem access point start failed, error {}",
                     static_cast<int>(res->error()));
      connection_event_.Emit(false);
    }
    Finish();
    return;
  }

  start_sub_ = op->result_event().Subscribe([this](auto const& res) {
    if (res) {
      AE_TELED_INFO("Modem access point start success");
      connection_event_.Emit(true);
    } else {
      AE_TELED_ERROR("Modem access point start failed, error {}",
                     static_cast<int>(res.error()));
      connection_event_.Emit(false);
    }
    Finish();
  });
}

ModemAccessPoint::ModemAccessPoint(ObjProp prop, ObjPtr<Aether> aether,
                                   ModemAdapter::ptr modem_adapter)
    : AccessPoint{prop},
      aether_{std::move(aether)},
      modem_adapter_{std::move(modem_adapter)} {}

ModemConnectAction& ModemAccessPoint::Connect() {
  AE_TELED_DEBUG("Make modem access point connection");

  // reuse connect action if it's in progress
  if (!connect_action_ || connect_action_->is_finished()) {
    connect_action_.emplace(*aether_.Load().as<Aether>(),
                            modem_adapter_->modem_driver());
  }
  return *connect_action_;
}

IModemDriver& ModemAccessPoint::modem_driver() {
  return modem_adapter_->modem_driver();
}

std::vector<ObjPtr<Channel>> ModemAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  AE_TELED_DEBUG("Generate modem channels");
  Aether::ptr aether = aether_;
  auto self = ModemAccessPoint::ptr::MakeFromThis(this);

  auto const& s = server.Load();
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(s->endpoints.size());
  for (auto const& endpoint : s->endpoints) {
    if (!FilterAddresses<AddrVersion::kIpV4, AddrVersion::kIpV6,
                         AddrVersion::kNamed>(endpoint)) {
      continue;
    }
    if (!FilterProtocol<Protocol::kTcp, Protocol::kUdp>(endpoint)) {
      continue;
    }
    channels.emplace_back(
        ModemChannel::ptr::Create(domain, aether, self, endpoint));
  }
  return channels;
}

}  // namespace ae
#endif

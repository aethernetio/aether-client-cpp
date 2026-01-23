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

#include "aether/access_points/wifi_access_point.h"

#include <utility>

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"
#include "aether/adapters/wifi_adapter.h"
#include "aether/channels/wifi_channel.h"
#include "aether/access_points/filter_endpoints.h"

namespace ae {

WifiConnectAction::WifiConnectAction(ActionContext action_context,
                                     WifiDriver& driver, WiFiAp const& wifi_ap,
                                     WiFiPowerSaveParam const& psp,
                                     WiFiBaseStation& base_station)
    : Action{action_context},
      driver_{&driver},
      wifi_ap_{wifi_ap},
      psp_{psp},
      base_station_{base_station},
      state_{State::kCheckIsConnected} {}

UpdateStatus WifiConnectAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kCheckIsConnected: {
        auto connected_to = driver_->connected_to();
        if (connected_to.ssid == wifi_ap_.creds.ssid) {
          state_ = State::kConnected;
        } else {
          state_ = State::kConnect;
        }
        Action::Trigger();
        break;
      }
      case State::kConnect: {
        driver_->Connect(wifi_ap_, psp_, base_station_);
        // TODO: Does connect should be async
        state_ = State::kConnected;
        Action::Trigger();
        break;
      }
      case State::kConnected:
        return UpdateStatus::Result();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  return {};
}

WifiConnectAction::State WifiConnectAction::state() const { return state_; }

WifiAccessPoint::WifiAccessPoint(ObjProp prop, ObjPtr<Aether> aether,
                                 ObjPtr<WifiAdapter> adapter,
                                 ObjPtr<IPoller> poller,
                                 ObjPtr<DnsResolver> resolver, WiFiAp wifi_ap,
                                 WiFiPowerSaveParam psp)
    : AccessPoint{prop},
      aether_{std::move(aether)},
      adapter_{std::move(adapter)},
      poller_{std::move(poller)},
      resolver_{std::move(resolver)},
      wifi_ap_{std::move(wifi_ap)},
      psp_{std::move(psp)} {}

std::vector<ObjPtr<Channel>> WifiAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  Aether::ptr aether = aether_;
  IPoller::ptr poller = poller_;
  DnsResolver::ptr resolver = resolver_;
  auto wifi_access_point = WifiAccessPoint::ptr::MakeFromThis(this);

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
    channels.emplace_back(WifiChannel::ptr::Create(
        domain, aether, poller, resolver, wifi_access_point, endpoint));
  }
  return channels;
}

ActionPtr<WifiConnectAction> WifiAccessPoint::Connect() {
  // reuse connect action if it's in progress
  auto adapter = WifiAdapter::ptr{adapter_}.Load();
  if (!connect_action_) {
    connect_action_ = ActionPtr<WifiConnectAction>{*aether_.Load().as<Aether>(),
                                                   adapter->driver(), wifi_ap_,
                                                   psp_, base_station_};
    connect_sub_ = connect_action_->FinishedEvent().Subscribe(
        [this]() { connect_action_.reset(); });
  }
  return connect_action_;
}

bool WifiAccessPoint::IsConnected() {
  auto adapter = WifiAdapter::ptr{adapter_}.Load();
  auto& driver = adapter->driver();
  auto connected_to = driver.connected_to();
  return connected_to.ssid == wifi_ap_.creds.ssid;
}

}  // namespace ae

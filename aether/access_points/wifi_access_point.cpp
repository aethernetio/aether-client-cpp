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
                                     WifiDriver& driver,
                                     WiFiInit const& wifi_init)
    : Action{action_context},
      driver_{&driver},
      wifi_init_{wifi_init},
      state_{State::kCheckIsConnected} {
  assert(wifi_init.wifi_creds.size() && "No WiFi credentials!");
}

UpdateStatus WifiConnectAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kCheckIsConnected: {
        auto connected_to = driver_->connected_to();
        if (connected_to.ssid == wifi_init_.wifi_creds.at(0).ssid) {
          state_ = State::kConnected;
        } else {
          state_ = State::kConnect;
        }
        Action::Trigger();
        break;
      }
      case State::kConnect: {
        driver_->Connect(wifi_init_);
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

WifiAccessPoint::WifiAccessPoint(ObjPtr<Aether> aether,
                                 ObjPtr<WifiAdapter> adapter,
                                 ObjPtr<IPoller> poller,
                                 ObjPtr<DnsResolver> resolver,
                                 ae::WiFiInit const& wifi_init, Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      adapter_{std::move(adapter)},
      poller_{std::move(poller)},
      resolver_{std::move(resolver)},
      wifi_init_{std::move(wifi_init)} {}

std::vector<ObjPtr<Channel>> WifiAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  Aether::ptr aether = aether_;
  IPoller::ptr poller = poller_;
  DnsResolver::ptr resolver = resolver_;
  WifiAccessPoint::ptr wifi_access_point = MakePtrFromThis(this);

  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(server->endpoints.size());
  for (auto const& endpoint : server->endpoints) {
    if (!FilterAddresses<AddrVersion::kIpV4, AddrVersion::kIpV6,
                         AddrVersion::kNamed>(endpoint)) {
      continue;
    }
    if (!FilterProtocol<Protocol::kTcp, Protocol::kUdp>(endpoint)) {
      continue;
    }
    channels.emplace_back(domain_->CreateObj<WifiChannel>(
        aether, poller, resolver, wifi_access_point, endpoint));
  }
  return channels;
}

ActionPtr<WifiConnectAction> WifiAccessPoint::Connect() {
  // reuse connect action if it's in progress
  if (!connect_action_) {
    connect_action_ = ActionPtr<WifiConnectAction>{
        *aether_.as<Aether>(), adapter_.as<WifiAdapter>()->driver(),
        wifi_init_};
    connect_sub_ = connect_action_->FinishedEvent().Subscribe(
        [this]() { connect_action_.reset(); });
  }
  return connect_action_;
}

bool WifiAccessPoint::IsConnected() {
  auto& driver = adapter_.as<WifiAdapter>()->driver();
  auto connected_to = driver.connected_to();
  return connected_to.ssid == wifi_init_.wifi_creds.at(0).ssid;
}

}  // namespace ae

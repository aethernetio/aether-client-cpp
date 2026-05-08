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

#if AE_SUPPORT_WIFIS
#  include <utility>

#  include "aether/aether.h"
#  include "aether/server.h"
#  include "aether/poller/poller.h"
#  include "aether/dns/dns_resolve.h"
#  include "aether/adapters/wifi_adapter.h"
#  include "aether/channels/wifi_channel.h"
#  include "aether/access_points/filter_endpoints.h"

namespace ae {

WifiConnectAction::WifiConnectAction(AeContext const& ae_context,
                                     WifiDriver& driver, WiFiAp wifi_ap,
                                     WiFiPowerSaveParam psp,
                                     WiFiBaseStation& base_station)
    : ae_context_{ae_context},
      driver_{&driver},
      wifi_ap_{std::move(wifi_ap)},
      psp_{std::move(psp)},
      base_station_{base_station},
      scheduler_sub_{
          ae_context_.scheduler().Task([this]() { EnsureConnected(); })} {}

WifiConnectAction::ConnectionEvent::Subscriber
WifiConnectAction::connection_event() {
  return EventSubscriber{connection_event_};
}

void WifiConnectAction::EnsureConnected() {
  auto connected_to = driver_->connected_to();
  if (connected_to.ssid != wifi_ap_.creds.ssid) {
    driver_->Connect(wifi_ap_, psp_, base_station_);
  }
  SetConnected(true);
}

void WifiConnectAction::SetConnected(bool is_connected) {
  connection_event_.Emit(is_connected);
  Finish();
}

WifiAccessPoint::WifiAccessPoint() = default;

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

WifiConnectAction& WifiAccessPoint::Connect() {
  // reuse connect action if it's in progress
  if (!connect_action_ || connect_action_->is_finished()) {
    auto driver = WifiAdapter::ptr{adapter_}.WithLoaded(
        [](auto const& a) { return &a->driver(); });
    assert(driver.has_value());

    connect_action_.emplace(*aether_.Load().as<Aether>(), **driver, wifi_ap_,
                            psp_, base_station_);
  }
  return *connect_action_;
}

bool WifiAccessPoint::IsConnected() {
  return WifiAdapter::ptr{adapter_}
      .WithLoaded([this](auto const& a) {
        auto& driver = a->driver();
        auto connected_to = driver.connected_to();
        return connected_to.ssid == wifi_ap_.creds.ssid;
      })
      .value_or(false);
}
}  // namespace ae
#endif

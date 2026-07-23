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

#  include "aether/access_points/filter_endpoints.h"
#  include "aether/adapters/wifi_adapter.h"
#  include "aether/aether.h"
#  include "aether/channels/wifi_channel.h"
#  include "aether/dns/dns_resolve.h"
#  include "aether/poller/poller.h"
#  include "aether/server.h"

#  include "aether/tele.h"

namespace ae {

WifiConnectAction::WifiConnectAction(
    AeContext const& ae_context, WifiAccessPoint& access_point,
    WifiDriver& driver, WiFiAp wifi_ap, std::optional<WiFiPowerSaveParam> psp,
    std::optional<WiFiBaseStation> base_station)
    : ae_context_{ae_context},
      access_point_{&access_point},
      driver_{&driver},
      wifi_ap_{std::move(wifi_ap)},
      psp_{std::move(psp)},
      base_station_{std::move(base_station)},
      scheduler_sub_{
          ae_context_.scheduler().Task([this]() { EnsureConnected(); })} {
  AE_TELED_DEBUG("WifiConnectAction created");
}

WifiConnectAction::ConnectionEvent::Subscriber
WifiConnectAction::connection_event() {
  return EventSubscriber{connection_event_};
}

void WifiConnectAction::EnsureConnected() {
  auto connected_to = driver_->connected_to();
  AE_TELED_DEBUG("Driver connected to {}",
                 connected_to.value_or("NOT CONNECTED"));
  // if already connected
  if (connected_to && (*connected_to == wifi_ap_.creds.ssid)) {
    SetConnected(true);
    return;
  }
  Connect();
}

void WifiConnectAction::Connect() {
  connect_sub_ = driver_->connect_res_event().Subscribe(
      [&](Result<WiFiBaseStation, int>&& res) {
        connect_sub_.Reset();
        if (res) {
          AE_TELED_INFO("Wifi connected");
          // save base station to access point
          access_point_->SetWifiBaseStation(std::move(res).value());
          SetConnected(true);
        } else {
          // retry without base station
          if (base_station_) {
            base_station_.reset();
            scheduler_sub_ = ae_context_.scheduler().Task([&]() { Connect(); });
            return;
          }
          AE_TELED_ERROR("Wifi did not connected with error {}", res.error());
          SetConnected(false);
        }
      });

  driver_->Connect(wifi_ap_, psp_, base_station_);
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
                                 std::optional<WiFiPowerSaveParam> psp)
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

    connect_action_.emplace(*aether_.Load().as<Aether>(), *this, **driver,
                            wifi_ap_, psp_, base_station_);
  }
  return *connect_action_;
}

bool WifiAccessPoint::IsConnected() {
  return WifiAdapter::ptr{adapter_}
      .WithLoaded([this](auto const& a) {
        auto& driver = a->driver();
        auto connected_to = driver.connected_to();
        return connected_to && (*connected_to == wifi_ap_.creds.ssid);
      })
      .value_or(false);
}

void WifiAccessPoint::SetWifiBaseStation(WiFiBaseStation&& wifi_base_station) {
  base_station_.emplace(std::move(wifi_base_station));
}
}  // namespace ae
#endif

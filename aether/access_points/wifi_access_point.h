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

#ifndef AETHER_ACCESS_POINTS_WIFI_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_WIFI_ACCESS_POINT_H_

#include <optional>

#include "aether/config.h"

#if AE_SUPPORT_WIFIS
#  include <cstdint>
#  include <optional>

#  include "aether/actions/action.h"
#  include "aether/ae_context.h"
#  include "aether/events/events.h"
#  include "aether/obj/obj_ptr.h"

#  include "aether/access_points/access_point.h"
#  include "aether/wifi/wifi_driver.h"

namespace ae {
class Aether;
class WifiAdapter;
class IPoller;
class DnsResolver;
class WifiAccessPoint;

class WifiConnectAction final : public Action {
 public:
  using ConnectionEvent = Event<void(bool)>;

  WifiConnectAction(AeContext const& ae_context, WifiAccessPoint& access_point,
                    WifiDriver& driver, WiFiAp wifi_ap,
                    std::optional<WiFiPowerSaveParam> psp);

  ConnectionEvent::Subscriber connection_event();

  std::uint32_t ExpId() const {
#  if defined(AE_EXP_DIAG)
    return exp_id_;
#  else
    return 0;
#  endif
  }

 private:
  void EnsureConnected();
  void Connect();
  void SetConnected(bool is_connected);

  AeContext ae_context_;
  WifiAccessPoint* access_point_;
  WifiDriver* driver_;
  WiFiAp wifi_ap_;
  std::optional<WiFiPowerSaveParam> psp_;
  ConnectionEvent connection_event_;
  TaskSubscription scheduler_sub_;
  Subscription connect_sub_;
#  if defined(AE_EXP_DIAG)
  std::uint32_t exp_id_{};
#  endif
};

class WifiAccessPoint final : public AccessPoint {
  AE_OBJECT(WifiAccessPoint, AccessPoint, 1)
  WifiAccessPoint();

 public:
  WifiAccessPoint(ObjProp prop, ObjPtr<Aether> aether,
                  ObjPtr<WifiAdapter> adapter, ObjPtr<IPoller> poller,
                  ObjPtr<DnsResolver> resolver, WiFiAp wifi_ap,
                  std::optional<WiFiPowerSaveParam> psp);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, adapter_, poller_, resolver_, wifi_ap_))

  // Legacy v0 may contain incompatible BSSID cache layouts. Do not deserialize
  // that blob; WifiAdapter recreates APs from persisted wifi_init_ credentials.
  template <typename Dnv>
  void Load(Version<0>, Dnv& /*dnv*/) {}

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    dnv(aether_, adapter_, poller_, resolver_, wifi_ap_);
  }

  template <typename Dnv>
  void Save(Version<0>, Dnv& dnv) const {
    dnv(base_);
    std::optional<LegacyWifiBaseStationV0> empty{};
    dnv(aether_, adapter_, poller_, resolver_, wifi_ap_, empty);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    dnv(aether_, adapter_, poller_, resolver_, wifi_ap_);
  }

  std::vector<ObjPtr<Channel>> GenerateChannels(
      ObjPtr<Server> const& server) override;

  /**
   * \brief Connect or ensure it's connected to current access point.
   */
  WifiConnectAction& Connect();

  bool IsConnected();

  bool HasWifiConfig() const { return !wifi_ap_.creds.ssid.empty(); }

 private:
  // Former v0 optional Wi-Fi BSSID/channel cache (written empty on Save v0).
  struct LegacyWifiBaseStationV0 {
    AE_REFLECT_MEMBERS(target_bssid, target_channel)
    uint8_t target_bssid[6]{};
    uint8_t target_channel{};
  };

  ObjPtr<Aether> aether_;
  ObjPtr<WifiAdapter> adapter_;
  ObjPtr<IPoller> poller_;
  ObjPtr<DnsResolver> resolver_;
  WiFiAp wifi_ap_{};
  std::optional<WiFiPowerSaveParam> psp_;
  std::optional<WifiConnectAction> connect_action_;
};
}  // namespace ae
#endif

#endif  // AETHER_ACCESS_POINTS_WIFI_ACCESS_POINT_H_

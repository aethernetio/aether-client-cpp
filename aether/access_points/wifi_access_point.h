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

#include <cstdint>

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

#include "aether/wifi/wifi_driver.h"
#include "aether/access_points/access_point.h"

namespace ae {
class Aether;
class WifiAdapter;
class IPoller;
class DnsResolver;

class WifiConnectAction final : public Action<WifiConnectAction> {
 public:
  enum class State : std::uint8_t {
    kCheckIsConnected,
    kConnect,
    kConnected,
    kFailed,
  };

  WifiConnectAction(ActionContext action_context, WifiDriver& driver,
                    WifiCreds const& creds);

  UpdateStatus Update();

  State state() const;

 private:
  WifiDriver* driver_;
  WifiCreds const& creds_;
  StateMachine<State> state_;
};

class WifiAccessPoint final : public AccessPoint {
  AE_OBJECT(WifiAccessPoint, AccessPoint, 0)
  WifiAccessPoint() = default;

 public:
  WifiAccessPoint(Aether& aether, WifiAdapter& adapter, IPoller& poller,
                  DnsResolver& resolver, WifiCreds wifi_creds, Domain* domain);

  std::vector<std::unique_ptr<Channel>> GenerateChannels(
      Server& server) override;

  /**
   * \brief Connect or ensure it's connected to current access point.
   */
  ActionPtr<WifiConnectAction> Connect();

  bool IsConnected();

  WifiCreds const& creds() const;

 private:
  Aether* aether_;
  WifiAdapter* adapter_;
  IPoller* poller_;
  DnsResolver* resolver_;
  WifiCreds wifi_creds_;
  ActionPtr<WifiConnectAction> connect_action_;
  Subscription connect_sub_;
};
}  // namespace ae

#endif  // AETHER_ACCESS_POINTS_WIFI_ACCESS_POINT_H_

/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/adapters/wifi_adapter.h"

#if AE_SUPPORT_WIFIS

#  include <cassert>
#  include <utility>

#  include "aether/aether.h"
#  include "aether/dns/dns_resolve.h"
#  include "aether/poller/poller.h"
#  include "aether/wifi/wifi_driver_factory.h"

#  include "aether/ae_exp_diag.h"
#  include "aether/tele.h"

namespace ae {
#  if defined AE_DISTILLATION
WifiAdapter::WifiAdapter(ObjProp prop, ObjPtr<Aether> aether,
                         IPoller::ptr poller, DnsResolver::ptr dns_resolver,
                         WiFiInit wifi_init)
    : ParentWifiAdapter{prop, std::move(aether), std::move(poller),
                        std::move(dns_resolver), std::move(wifi_init)}
#    if defined(AE_EXP_DIAG)
      ,
      exp_id_{AeExpNextId()}
#    endif
{
  AE_EXP_LC("WifiAdapter", ExpId(), this, 0, "ctor", "");
  AE_TELED_DEBUG("Wifi instance created!");
}
#  endif  // AE_DISTILLATION

std::vector<AccessPoint::ptr> WifiAdapter::access_points() {
  bool recreate = access_points_.empty();
  if (!recreate) {
    for (auto const& ap : access_points_) {
      bool has_config = false;
      WifiAccessPoint::ptr{ap}.WithLoaded([&](auto const& p) {
        has_config = static_cast<bool>(p) && p->HasWifiConfig();
      });
      if (!has_config) {
        recreate = true;
        break;
      }
    }
  }

  if (recreate) {
    access_points_.clear();
    auto self_ptr = WifiAdapter::ptr::MakeFromThis(this);

    for (const auto& ap : wifi_init_.wifi_ap) {
      auto access_point =
          WifiAccessPoint::ptr::Create(domain, aether_, self_ptr, poller_,
                                       dns_resolver_, ap, wifi_init_.psp);
      access_points_.emplace_back(std::move(access_point));
    }
  }

  return access_points_;
}

WifiDriver& WifiAdapter::driver() {
  if (!wifi_driver_) {
    wifi_driver_ = WifiDriverFactory::CreateWifiDriver(*aether_);
    AE_EXP_LC("WifiAdapter", ExpId(), this, 0, "driver_create", "driver=%p",
              static_cast<void*>(wifi_driver_.get()));
  } else {
    AE_EXP_LC("WifiAdapter", ExpId(), this, 0, "driver_reuse", "driver=%p",
              static_cast<void*>(wifi_driver_.get()));
  }
  return *wifi_driver_;
}
}  // namespace ae
#endif

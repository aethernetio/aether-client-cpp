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

#include "aether/adapters/parent_wifi.h"

#include <utility>

#include "aether/aether.h"
#include "aether/poller/poller.h"
#include "aether/dns/dns_resolve.h"

namespace ae {

#if defined AE_DISTILLATION
ParentWifiAdapter::ParentWifiAdapter(ObjPtr<Aether> aether,
                                     ObjPtr<IPoller> poller,
                                     ObjPtr<DnsResolver> dns_resolver,
                                     std::string ssid, std::string pass,
                                     Domain* domain)
    : Adapter{domain},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      dns_resolver_{std::move(dns_resolver)},
      ssid_{std::move(ssid)},
      pass_{std::move(pass)} {}
#endif  // AE_DISTILLATION

} /* namespace ae */

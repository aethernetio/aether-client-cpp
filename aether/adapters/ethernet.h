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

#ifndef AETHER_ADAPTERS_ETHERNET_H_
#define AETHER_ADAPTERS_ETHERNET_H_

#include "aether/poller/poller.h"
#include "aether/obj/dummy_obj.h"    // IWYU pragma: keep
#include "aether/dns/dns_resolve.h"  // IWYU pragma: keep
#include "aether/adapters/adapter.h"

#include "aether/access_points/ethernet_access_point.h"

namespace ae {
class Aether;
/**
 * \brief The Ethernet interface adapter.
 * This is the most common and suitable for most devices adapter. It's not tied
 * to particular ethernet device, and does not require any additional setup.
 * It's just creates transport the most common way for your system (desktop or
 * mobile).
 */
class EthernetAdapter final : public Adapter {
  AE_OBJECT(EthernetAdapter, Adapter, 0)

  EthernetAdapter() = default;

 public:
#ifdef AE_DISTILLATION
  EthernetAdapter(ObjProp prop, ObjPtr<Aether> aether, IPoller::ptr poller,
                  DnsResolver::ptr dns_resolver);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT(AE_MMBRS(ethernet_access_point_))

  std::vector<AccessPoint::ptr> access_points() override;

 private:
  EthernetAccessPoint::ptr ethernet_access_point_;
};
}  // namespace ae

#endif  // AETHER_ADAPTERS_ETHERNET_H_

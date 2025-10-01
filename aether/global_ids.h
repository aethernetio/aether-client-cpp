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

#ifndef AETHER_GLOBAL_IDS_H_
#define AETHER_GLOBAL_IDS_H_

#include "aether/obj/obj_id.h"

namespace ae {
static constexpr ObjId kGlobalIdAdaptersOffset{1000};
static constexpr ObjId kGlobalIdFactoriesOffset{2000};
static constexpr ObjId kGlobalIdCryptoOffset{3000};
static constexpr ObjId kGlobalIdPollerOffset{4000};
static constexpr ObjId kGlobalIdDnsResolverOffset{5000};

struct GlobalId {
  static constexpr ObjId kAether{1};
  static constexpr ObjId kRegistrationCloud{2};
  static constexpr ObjId kTeleStatistics{3};
  static constexpr ObjId kEthernetAdapter = kGlobalIdAdaptersOffset + 1;
  static constexpr ObjId kLanAdapter = kGlobalIdAdaptersOffset + 2;
  static constexpr ObjId kWiFiAdapter = kGlobalIdAdaptersOffset + 3;
  static constexpr ObjId kRegisterWifiAdapter = kGlobalIdAdaptersOffset + 4;
  static constexpr ObjId kModemAdapter = kGlobalIdAdaptersOffset + 5;
  static constexpr ObjId kRegisterModemAdapter = kGlobalIdAdaptersOffset + 6;

  static constexpr ObjId kServerFactory = kGlobalIdFactoriesOffset + 0;
  static constexpr ObjId kClientFactory = kGlobalIdFactoriesOffset + 1;
  static constexpr ObjId kIpFactory = kGlobalIdFactoriesOffset + 2;
  static constexpr ObjId kChannelFactory = kGlobalIdFactoriesOffset + 3;
  static constexpr ObjId kProxyFactory = kGlobalIdFactoriesOffset + 5;
  static constexpr ObjId kStatisticsFactory = kGlobalIdFactoriesOffset + 6;

  static constexpr ObjId kPoller = kGlobalIdPollerOffset + 0;
  static constexpr ObjId kDnsResolver = kGlobalIdDnsResolverOffset + 0;

  static constexpr ObjId kCrypto = kGlobalIdCryptoOffset + 0;
};

}  // namespace ae

#endif  // AETHER_GLOBAL_IDS_H_ */

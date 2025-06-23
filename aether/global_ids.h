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

namespace ae {

constexpr int kGlobalIdAdatpersOffset = 1000;
constexpr int kGlobalIdFactoriesOffset = 2000;
constexpr int kGlobalIdCryptoOffset = 3000;
constexpr int kGlobalIdPollerOffset = 4000;
constexpr int kGlobalIdDnsResolverOffset = 5000;

enum GlobalId {
  kAether = 1,
  kRegistrationCloud = 2,
  kTeleStatistics = 3,
  kEthernetAdapter = kGlobalIdAdatpersOffset + 1,
  kLanAdapter = kGlobalIdAdatpersOffset + 2,
  kEsp32WiFiAdapter = kGlobalIdAdatpersOffset + 3,
  kRegisterWifiAdapter = kGlobalIdAdatpersOffset + 4,
  kModemAdapter = kGlobalIdAdatpersOffset + 5,
  kRegisterModemAdapter = kGlobalIdAdatpersOffset + 6,

  kServerFactory = kGlobalIdFactoriesOffset + 0,
  kClientFactory = kGlobalIdFactoriesOffset + 1,
  kIpFactory = kGlobalIdFactoriesOffset + 2,
  kChannelFactory = kGlobalIdFactoriesOffset + 3,
  kCloudFactory = kGlobalIdFactoriesOffset + 4,
  kProxyFactory = kGlobalIdFactoriesOffset + 5,
  kStatisticsFactory = kGlobalIdFactoriesOffset + 6,

  kPoller = kGlobalIdPollerOffset + 0,
  kDnsResolver = kGlobalIdDnsResolverOffset + 0,

  kCrypto = kGlobalIdCryptoOffset + 0,
};

}  // namespace ae

#endif  // AETHER_GLOBAL_IDS_H_ */

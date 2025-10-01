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

#include "aether/access_points/modem_access_point.h"

#include "aether/aether.h"

#include "aether/channels/modem_channel.h"

namespace ae {
ModemAccessPoint::ModemAccessPoint(ObjPtr<Aether> aether,
                                   IModemDriver::ptr modem_driver,
                                   Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      modem_driver_{std::move(modem_driver)} {}

std::vector<ObjPtr<Channel>> ModemAccessPoint::GenerateChannels(
    std::vector<UnifiedAddress> const& endpoints) {
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(endpoints.size());
  Aether::ptr aether = aether_;
  for (auto const& endpoint : endpoints) {
    channels.emplace_back(
        domain_->CreateObj<ModemChannel>(aether, modem_driver_, endpoint));
  }
  return channels;
}

}  // namespace ae

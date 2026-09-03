/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/access_points/browser_access_point.h"

#include <utility>

#include "aether/aether.h"
#include "aether/server.h"
#include "aether/access_points/filter_endpoints.h"
#include "aether/channels/browser_channel.h"

namespace ae {
BrowserAccessPoint::BrowserAccessPoint(ObjProp prop, ObjPtr<Aether> aether)
    : AccessPoint{prop}, aether_{std::move(aether)} {}

std::vector<ObjPtr<Channel>> BrowserAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  Aether::ptr aether = aether_;

  auto const& s = server.Load();
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(s->endpoints.size());
  for (auto const& endpoint : s->endpoints) {
    auto const version = endpoint.address.Index();
    // BrowserAddr preferred; NamedAddr allowed as milestone UI override
    // (even when cloud DNS is disabled).
    if (version != AddrVersion::kBrowser && version != AddrVersion::kNamed) {
      continue;
    }
    if (!FilterProtocol<Protocol::kHttp, Protocol::kHttps, Protocol::kWebSocket,
                        Protocol::kWebSocketSecure>(endpoint)) {
      continue;
    }
    channels.emplace_back(
        BrowserChannel::ptr::Create(domain, aether, endpoint));
  }
  return channels;
}

}  // namespace ae

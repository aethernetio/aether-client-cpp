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

#ifndef AETHER_TRANSPORT_BROWSER_BROWSER_ENDPOINT_H_
#define AETHER_TRANSPORT_BROWSER_BROWSER_ENDPOINT_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "aether/types/address.h"

namespace ae {
namespace browser_endpoint_internal {

inline constexpr std::string_view kDefaultWsPath = "/aether/v1/ws";
inline constexpr std::string_view kDefaultHttpApiRoot = "/aether/v1";

inline bool IsWebSocketProtocol(Protocol protocol) noexcept {
  return protocol == Protocol::kWebSocket ||
         protocol == Protocol::kWebSocketSecure;
}

inline bool IsHttpProtocol(Protocol protocol) noexcept {
  return protocol == Protocol::kHttp || protocol == Protocol::kHttps;
}

inline bool IsSecureProtocol(Protocol protocol) noexcept {
  return protocol == Protocol::kHttps ||
         protocol == Protocol::kWebSocketSecure;
}

inline char const* SchemeFor(Protocol protocol) noexcept {
  switch (protocol) {
    case Protocol::kWebSocket:
      return "ws";
    case Protocol::kWebSocketSecure:
      return "wss";
    case Protocol::kHttp:
      return "http";
    case Protocol::kHttps:
      return "https";
    default:
      return "";
  }
}

inline bool IsDefaultPort(Protocol protocol, std::uint16_t port) noexcept {
  if (port == 0) {
    return true;
  }
  if (IsSecureProtocol(protocol)) {
    return port == 443;
  }
  return port == 80;
}

inline std::string EnsureLeadingSlash(std::string_view path) {
  if (path.empty()) {
    return {};
  }
  if (path.front() == '/') {
    return std::string{path};
  }
  std::string out;
  out.reserve(path.size() + 1);
  out.push_back('/');
  out.append(path);
  return out;
}

inline std::string JoinUrl(std::string_view scheme, std::string_view host,
                           std::uint16_t port, Protocol protocol,
                           std::string_view path_and_query) {
  std::string url;
  url.reserve(scheme.size() + host.size() + path_and_query.size() + 16);
  url.append(scheme);
  url.append("://");
  url.append(host);
  if (!IsDefaultPort(protocol, port) && port != 0) {
    url.push_back(':');
    url.append(std::to_string(port));
  }
  url.append(path_and_query);
  return url;
}

inline BrowserAddr ResolveBrowserAddr(Endpoint const& endpoint) {
  if (endpoint.address.Index() == AddrVersion::kBrowser) {
    return endpoint.address.Get<BrowserAddr>();
  }
  // Milestone override: NamedAddr hostname with default browser paths.
  if (endpoint.address.Index() == AddrVersion::kNamed) {
    BrowserAddr addr;
    addr.hostname = endpoint.address.Get<NamedAddr>().name;
    if (IsWebSocketProtocol(endpoint.protocol)) {
      addr.path = std::string{kDefaultWsPath};
    } else {
      addr.path = std::string{kDefaultHttpApiRoot};
    }
    return addr;
  }
  return BrowserAddr{};
}

inline std::string BuildWebSocketUrl(Endpoint const& endpoint) {
  auto const addr = ResolveBrowserAddr(endpoint);
  auto path = EnsureLeadingSlash(addr.path.empty() ? kDefaultWsPath : addr.path);
  if (!addr.gateway_target.empty()) {
    auto const qpos = path.find('?');
    if (qpos == std::string::npos) {
      path.append("?target=");
      path.append(addr.gateway_target);
    } else {
      path.append("&target=");
      path.append(addr.gateway_target);
    }
  }
  return JoinUrl(SchemeFor(endpoint.protocol), addr.hostname, endpoint.port,
                 endpoint.protocol, path);
}

inline std::string BuildHttpOrigin(Endpoint const& endpoint) {
  auto const addr = ResolveBrowserAddr(endpoint);
  return JoinUrl(SchemeFor(endpoint.protocol), addr.hostname, endpoint.port,
                 endpoint.protocol, "");
}

inline std::string BuildHttpApiRoot(Endpoint const& endpoint) {
  auto const addr = ResolveBrowserAddr(endpoint);
  auto root = EnsureLeadingSlash(
      addr.path.empty() ? kDefaultHttpApiRoot : std::string_view{addr.path});
  // Trim trailing slash for consistent joins.
  while (!root.empty() && root.back() == '/') {
    root.pop_back();
  }
  if (root.empty()) {
    root = std::string{kDefaultHttpApiRoot};
  }
  return BuildHttpOrigin(endpoint) + root;
}

inline std::string BuildHttpConnectUrl(Endpoint const& endpoint) {
  return BuildHttpApiRoot(endpoint) + "/connect";
}

inline std::string BuildHttpSendUrl(Endpoint const& endpoint,
                                    std::string_view session_id) {
  std::string url = BuildHttpApiRoot(endpoint);
  url.append("/session/");
  url.append(session_id);
  url.append("/send");
  return url;
}

inline std::string BuildHttpReceiveUrl(Endpoint const& endpoint,
                                       std::string_view session_id) {
  std::string url = BuildHttpApiRoot(endpoint);
  url.append("/session/");
  url.append(session_id);
  url.append("/receive");
  return url;
}

inline std::string BuildHttpCloseUrl(Endpoint const& endpoint,
                                     std::string_view session_id) {
  std::string url = BuildHttpApiRoot(endpoint);
  url.append("/session/");
  url.append(session_id);
  url.append("/close");
  return url;
}

inline std::string BuildConnectTargetJson(Endpoint const& endpoint) {
  auto const addr = ResolveBrowserAddr(endpoint);
  if (addr.gateway_target.empty()) {
    return "{}";
  }
  std::string json;
  json.reserve(addr.gateway_target.size() + 16);
  json.append("{\"target\":\"");
  json.append(addr.gateway_target);
  json.append("\"}");
  return json;
}

}  // namespace browser_endpoint_internal

using browser_endpoint_internal::BuildConnectTargetJson;
using browser_endpoint_internal::BuildHttpCloseUrl;
using browser_endpoint_internal::BuildHttpConnectUrl;
using browser_endpoint_internal::BuildHttpOrigin;
using browser_endpoint_internal::BuildHttpReceiveUrl;
using browser_endpoint_internal::BuildHttpSendUrl;
using browser_endpoint_internal::BuildWebSocketUrl;
using browser_endpoint_internal::IsHttpProtocol;
using browser_endpoint_internal::IsWebSocketProtocol;
using browser_endpoint_internal::ResolveBrowserAddr;

}  // namespace ae

#endif  // AETHER_TRANSPORT_BROWSER_BROWSER_ENDPOINT_H_

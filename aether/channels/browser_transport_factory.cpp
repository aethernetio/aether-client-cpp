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

#include "aether/channels/browser_transport_factory.h"

#include <cassert>

#include "aether/transport/browser/browser_endpoint.h"
#include "aether/transport/browser/browser_http_transport.h"
#include "aether/transport/browser/browser_websocket_transport.h"

namespace ae {

std::unique_ptr<ByteIStream> BrowserTransportFactory::Create(
    AeContext const& ae_context, Endpoint endpoint) {
  assert((endpoint.address.Index() == AddrVersion::kBrowser ||
          endpoint.address.Index() == AddrVersion::kNamed) &&
         "Browser transport requires BrowserAddr or NamedAddr");

  switch (endpoint.protocol) {
    case Protocol::kWebSocket:
    case Protocol::kWebSocketSecure:
      return std::make_unique<BrowserWebSocketTransport>(ae_context,
                                                         std::move(endpoint));
    case Protocol::kHttp:
    case Protocol::kHttps:
      return std::make_unique<BrowserHttpTransport>(ae_context,
                                                    std::move(endpoint));
    default:
      assert(false && "Unsupported browser protocol");
      return nullptr;
  }
}

}  // namespace ae

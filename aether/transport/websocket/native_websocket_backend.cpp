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

#include "aether/config.h"
#if AE_SUPPORT_WEBSOCKET && defined(AE_NATIVE_WEBSOCKET)
#  include "aether/transport/websocket/websocket_backend.h"

#  include <filesystem>
#  include <fstream>
#  include <iterator>
#  include <mutex>
#  include <utility>

#  include <ixwebsocket/IXNetSystem.h>
#  include <ixwebsocket/IXWebSocket.h>

#  include "aether/transport/websocket/websocket.h"

namespace ae {
namespace native_websocket_backend_internal {
std::string SystemTrust() {
#  if defined(__ANDROID__)
  // Public platform trust anchors, including the updatable Conscrypt store.
  // An empty/unreadable store fails TLS verification; never fall back to NONE.
  std::string certificates;
  for (auto const* directory : {"/apex/com.android.conscrypt/cacerts",
                                "/system/etc/security/cacerts"}) {
    std::error_code error;
    std::filesystem::directory_iterator it{directory, error};
    if (error) continue;
    for (auto const& entry : it) {
      std::ifstream file{entry.path()};
      std::string pem{std::istreambuf_iterator<char>{file}, {}};
      if (pem.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
        certificates += pem;
        certificates += '\n';
      }
    }
    if (!certificates.empty()) return certificates;
  }
  return "SYSTEM";
#  elif defined(__linux__)
  for (auto const* path :
       {"/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem"}) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) return path;
  }
  return "SYSTEM";
#  else
  return "SYSTEM";
#  endif
}

class Backend final : public IWebSocketBackend {
 public:
  explicit Backend(std::string const& url) {
    // Winsock's ref-counted startup/cleanup also coexists with
    // SocketInitializer.
    initialized_ = ix::initNetSystem();
    if (!initialized_) {
      state_ = State::kError;
      return;
    }
    socket_.setUrl(url);
    static auto const trust = SystemTrust();
    ix::SocketTLSOptions tls;
    tls.caFile = trust;
    socket_.setTLSOptions(tls);
    socket_.disableAutomaticReconnection();
    socket_.disablePerMessageDeflate();
    socket_.setOnMessageCallback(
        [this](ix::WebSocketMessagePtr const& message) {
          auto lock = std::scoped_lock{lock_};
          switch (message->type) {
            case ix::WebSocketMessageType::Open:
              state_ = State::kOpen;
              break;
            case ix::WebSocketMessageType::Close:
              state_ = State::kClosed;
              break;
            case ix::WebSocketMessageType::Error:
              state_ = State::kError;
              break;
            case ix::WebSocketMessageType::Message:
              if (message->str.empty()) break;
              if (!message->binary ||
                  message->str.size() >
                      WebSocketTransport::kMaxBufferedBytes - received_bytes_ ||
                  received_.size() >= WebSocketTransport::kMaxPendingMessages) {
                state_ = State::kError;
                break;
              }
              received_bytes_ += message->str.size();
              received_.emplace_back(message->str.begin(), message->str.end());
              break;
            default:
              break;  // Ping/Pong are handled by the RFC 6455 implementation.
          }
        });
    socket_.start();
  }
  ~Backend() override {
    socket_.stop();  // joins the receive thread before callback storage dies
    if (initialized_) ix::uninitNetSystem();
  }
  Update Poll() override {
    auto lock = std::scoped_lock{lock_};
    received_bytes_ = 0;
    return {state_, std::exchange(received_, {})};
  }
  bool Send(DataBuffer const& data) override {
    // IX uses nonblocking sockets and flushes pending bytes on its I/O thread.
    return socket_.sendBinary(std::string(data.begin(), data.end())).success;
  }
  std::size_t BufferedAmount() const override {
    return socket_.bufferedAmount();
  }

 private:
  ix::WebSocket socket_;
  bool initialized_{};
  std::mutex lock_;
  State state_{State::kConnecting};
  std::vector<DataBuffer> received_;
  std::size_t received_bytes_{};
};
}  // namespace native_websocket_backend_internal
std::unique_ptr<IWebSocketBackend> MakeWebSocketBackend(
    std::string const& url) {
  return std::make_unique<native_websocket_backend_internal::Backend>(url);
}
}  // namespace ae
#endif

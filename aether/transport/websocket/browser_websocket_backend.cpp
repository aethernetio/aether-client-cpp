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
#if AE_SUPPORT_WEBSOCKET && defined(__EMSCRIPTEN__)
#  include "aether/transport/websocket/websocket_backend.h"

#  include <emscripten/websocket.h>
#  include <mutex>
#  include <utility>

#  include "aether/transport/websocket/websocket.h"

namespace ae {
namespace browser_websocket_backend_internal {
class Backend final : public IWebSocketBackend {
 public:
  explicit Backend(std::string const& url) {
    EmscriptenWebSocketCreateAttributes attributes{};
    attributes.url = url.c_str();
    attributes.createOnMainThread = true;
    socket_ = emscripten_websocket_new(&attributes);
    if (socket_ <= 0) {
      state_ = State::kError;
      return;
    }
    // Main-thread callbacks keep receiving while the model pthread waits on its
    // scheduler. They touch only this synchronized, bounded runtime mailbox.
    emscripten_websocket_set_onopen_callback_on_thread(
        socket_, this,
        [](int, EmscriptenWebSocketOpenEvent const*, void* context) {
          auto& self = *static_cast<Backend*>(context);
          auto lock = std::scoped_lock{self.lock_};
          self.state_ = State::kOpen;
          return true;
        },
        EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD);
    emscripten_websocket_set_onerror_callback_on_thread(
        socket_, this,
        [](int, EmscriptenWebSocketErrorEvent const*, void* context) {
          auto& self = *static_cast<Backend*>(context);
          auto lock = std::scoped_lock{self.lock_};
          self.state_ = State::kError;
          return true;
        },
        EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD);
    emscripten_websocket_set_onclose_callback_on_thread(
        socket_, this,
        [](int, EmscriptenWebSocketCloseEvent const*, void* context) {
          auto& self = *static_cast<Backend*>(context);
          auto lock = std::scoped_lock{self.lock_};
          self.state_ = State::kClosed;
          return true;
        },
        EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD);
    emscripten_websocket_set_onmessage_callback_on_thread(
        socket_, this,
        [](int, EmscriptenWebSocketMessageEvent const* message, void* context) {
          auto& self = *static_cast<Backend*>(context);
          auto lock = std::scoped_lock{self.lock_};
          if (message->numBytes == 0) return true;
          if (message->isText ||
              message->numBytes > WebSocketTransport::kMaxBufferedBytes -
                                      self.received_bytes_ ||
              self.received_.size() >=
                  WebSocketTransport::kMaxPendingMessages) {
            self.state_ = State::kError;
          } else {
            auto* bytes = static_cast<std::uint8_t const*>(message->data);
            self.received_.emplace_back(bytes, bytes + message->numBytes);
            self.received_bytes_ += message->numBytes;
          }
          return true;
        },
        EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD);
  }
  ~Backend() override {
    if (socket_ <= 0) return;
    // Removing handlers and deleting are synchronous proxies to the socket's
    // owning browser thread; no queued callback retains the C++ context.
    emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr);
    emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr);
    emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr);
    emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr);
    emscripten_websocket_close(socket_, 1000, "");
    emscripten_websocket_delete(socket_);
  }
  Update Poll() override {
    auto lock = std::scoped_lock{lock_};
    received_bytes_ = 0;
    return {state_, std::exchange(received_, {})};
  }
  bool Send(DataBuffer const& data) override {
    return emscripten_websocket_send_binary(
               socket_, const_cast<std::uint8_t*>(data.data()), data.size()) ==
           EMSCRIPTEN_RESULT_SUCCESS;
  }
  std::size_t BufferedAmount() const override {
    std::size_t count{};
    if (emscripten_websocket_get_buffered_amount(socket_, &count) !=
        EMSCRIPTEN_RESULT_SUCCESS)
      return WebSocketTransport::kMaxBufferedBytes;
    return static_cast<std::size_t>(count);
  }

 private:
  EMSCRIPTEN_WEBSOCKET_T socket_{};
  mutable std::mutex lock_;
  State state_{State::kConnecting};
  std::vector<DataBuffer> received_;
  std::size_t received_bytes_{};
};
}  // namespace browser_websocket_backend_internal
std::unique_ptr<IWebSocketBackend> MakeWebSocketBackend(
    std::string const& url) {
  return std::make_unique<browser_websocket_backend_internal::Backend>(url);
}
}  // namespace ae
#endif

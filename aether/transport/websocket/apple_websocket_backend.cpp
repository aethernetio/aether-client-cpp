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
#if AE_SUPPORT_WEBSOCKET && defined(AE_APPLE_WEBSOCKET)
#  include "aether/transport/websocket/websocket_backend.h"

#  include <Network/Network.h>
#  include <dispatch/dispatch.h>

#  include <cstring>
#  include <memory>
#  include <mutex>
#  include <utility>

#  include "aether/transport/websocket/websocket.h"

namespace ae {
namespace apple_websocket_backend_internal {
using State = IWebSocketBackend::State;

// Shared only across the model thread and Dispatch callbacks. In particular,
// this state does not own the connection, backend, scheduler or object graph.
// std::shared_ptr supplies thread-safe lifetime management during cancellation.
struct CallbackState {
  std::mutex lock;
  State state{State::kConnecting};
  bool closing{};
  std::vector<DataBuffer> received;
  std::size_t received_bytes{};
  std::size_t sending_bytes{};

  bool Terminal() const {
    return closing || state == State::kClosed || state == State::kError;
  }
};

// Called with CallbackState::lock held. The mailbox limit is checked before
// allocating a copy of the system-owned message.
void StoreBinary(CallbackState& state, dispatch_data_t content) {
  auto size = content != nullptr ? dispatch_data_get_size(content) : 0;
  if (size == 0) {
    return;
  }
  if (size > WebSocketTransport::kMaxBufferedBytes - state.received_bytes ||
      state.received.size() >= WebSocketTransport::kMaxPendingMessages) {
    state.state = State::kError;
    return;
  }
  DataBuffer bytes(size);
  auto* destination = bytes.data();
  dispatch_data_apply(content, ^bool(dispatch_data_t, std::size_t offset,
                                     void const* region, std::size_t length) {
    std::memcpy(destination + offset, region, length);
    return true;
  });
  state.received_bytes += size;
  state.received.push_back(std::move(bytes));
}

API_AVAILABLE(macos(10.15), ios(13.0), tvos(13.0), watchos(6.0))
void HandleMessage(CallbackState& state, dispatch_data_t content,
                   nw_content_context_t context, bool complete,
                   nw_error_t error) {
  // The receive callback holds the lock and has checked for a terminal state.
  auto* definition = nw_protocol_copy_ws_definition();
  auto* metadata =
      context != nullptr
          ? nw_content_context_copy_protocol_metadata(context, definition)
          : nullptr;
  nw_release(definition);
  if (metadata == nullptr) {
    state.state = error == nullptr ? State::kClosed : State::kError;
    return;
  }
  auto opcode = nw_ws_metadata_get_opcode(metadata);
  nw_release(metadata);
  switch (opcode) {
    case nw_ws_opcode_binary:
      // NW handles continuation frames internally. Only complete messages are
      // accepted; the Aether parser independently reassembles its own packets.
      if (complete) {
        StoreBinary(state, content);
      } else {
        state.state = State::kError;
      }
      break;
    case nw_ws_opcode_close:
      state.state = State::kClosed;
      break;
    case nw_ws_opcode_ping:
    case nw_ws_opcode_pong:
      break;
    default:
      state.state = State::kError;  // Text is not an Aether packet.
      break;
  }
  // Preserve a final binary message delivered alongside an error.
  if (error != nullptr && !state.Terminal()) {
    state.state = State::kError;
  }
}

// The connection has one explicitly retained reference per outstanding receive.
// Cancel completes that receive, ending this chain without a connection/block
// ownership cycle. No callback captures a Backend pointer.
API_AVAILABLE(macos(10.15), ios(13.0), tvos(13.0), watchos(6.0))
void Receive(nw_connection_t connection,
             std::shared_ptr<CallbackState> const& state) {
  nw_retain(connection);
  // Capture a value, never the reference parameter, across this async boundary.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  auto callback_state = state;
  nw_connection_receive_message(
      connection, ^(dispatch_data_t content, nw_content_context_t context,
                    bool complete, nw_error_t error) {
        bool again = false;
        {
          auto lock = std::scoped_lock{callback_state->lock};
          if (!callback_state->Terminal()) {
            HandleMessage(*callback_state, content, context, complete, error);
            again = !callback_state->Terminal();
          }
        }
        if (again) {
          Receive(connection, callback_state);
        } else {
          nw_connection_cancel(connection);
        }
        nw_release(connection);
      });
}

class API_AVAILABLE(macos(10.15), ios(13.0), tvos(13.0), watchos(6.0))
    Backend final : public IWebSocketBackend {
 public:
  explicit Backend(std::string const& url) {
    auto const secure = url.starts_with("wss://");
    if (!secure && !url.starts_with("ws://")) {
      state_->state = State::kError;
      return;
    }
    auto* endpoint = nw_endpoint_create_url(url.c_str());
    if (endpoint == nullptr) {
      state_->state = State::kError;
      return;
    }
    // Passing the original URL lets Network.framework resolve the hostname and
    // use it for SNI and system certificate validation. No custom trust block.
    auto* parameters = nw_parameters_create_secure_tcp(
        secure ? NW_PARAMETERS_DEFAULT_CONFIGURATION
               : NW_PARAMETERS_DISABLE_PROTOCOL,
        NW_PARAMETERS_DEFAULT_CONFIGURATION);
    auto* websocket = nw_ws_create_options(nw_ws_version_13);
    nw_ws_options_set_auto_reply_ping(websocket, true);
    // A WS message may contain several separately bounded Aether packets.
    nw_ws_options_set_maximum_message_size(
        websocket, WebSocketTransport::kMaxBufferedBytes);
    auto* stack = nw_parameters_copy_default_protocol_stack(parameters);
    nw_protocol_stack_prepend_application_protocol(stack, websocket);
    connection_ = nw_connection_create(endpoint, parameters);
    nw_release(stack);
    nw_release(websocket);
    nw_release(parameters);
    nw_release(endpoint);
    if (connection_ == nullptr) {
      state_->state = State::kError;
      return;
    }

    auto* queue =
        dispatch_queue_create("io.aether.websocket", DISPATCH_QUEUE_SERIAL);
    nw_connection_set_queue(connection_, queue);
    dispatch_release(queue);
    auto state = state_;
    nw_connection_set_state_changed_handler(
        connection_, ^(nw_connection_state_t status, nw_error_t) {
          auto lock = std::scoped_lock{state->lock};
          if (state->Terminal()) {
            return;
          }
          switch (status) {
            case nw_connection_state_ready:
              state->state = State::kOpen;
              break;
            case nw_connection_state_waiting:
            case nw_connection_state_failed:
              // Aether owns retry/failover. Do not leave a failed attempt
              // waiting indefinitely for Network.framework's path recovery.
              state->state = State::kError;
              break;
            case nw_connection_state_cancelled:
              state->state = State::kClosed;
              break;
            default:
              state->state = State::kConnecting;
              break;
          }
        });
    nw_connection_start(connection_);
    Receive(connection_, state_);
  }

  Backend(Backend const&) = delete;
  Backend& operator=(Backend const&) = delete;

  ~Backend() override {
    {
      auto lock = std::scoped_lock{state_->lock};
      state_->closing = true;
    }
    if (connection_ != nullptr) {
      // Cancellation is asynchronous. Detached callback state and any pending
      // receive's retain survive until NW completes them. Never block this
      // thread.
      nw_connection_cancel(connection_);
      nw_release(connection_);
    }
  }

  Update Poll() override {
    auto lock = std::scoped_lock{state_->lock};
    state_->received_bytes = 0;
    return {state_->state, std::exchange(state_->received, {})};
  }

  bool Send(DataBuffer const& data) override {
    auto state = state_;
    auto size = data.size();
    {
      auto lock = std::scoped_lock{state->lock};
      if (state->Terminal() || state->state != State::kOpen || size == 0 ||
          size > WebSocketTransport::kMaxBufferedBytes - state->sending_bytes) {
        return false;
      }
      state->sending_bytes += size;
    }
    // DEFAULT copies the bytes before returning: Send's caller may immediately
    // release its buffer. NW owns the dispatch data until send completion.
    auto* content = dispatch_data_create(data.data(), size, nullptr,
                                         DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    auto* metadata = nw_ws_create_metadata(nw_ws_opcode_binary);
    auto* context = nw_content_context_create("aether.binary");
    nw_content_context_set_metadata_for_protocol(context, metadata);
    nw_connection_send(connection_, content, context, true,
                       ^(nw_error_t error) {
                         auto lock = std::scoped_lock{state->lock};
                         state->sending_bytes -= size;
                         if (error != nullptr && !state->Terminal()) {
                           state->state = State::kError;
                         }
                       });
    nw_release(context);
    nw_release(metadata);
    dispatch_release(content);
    return true;  // Local acceptance, not peer delivery.
  }

  std::size_t BufferedAmount() const override {
    auto lock = std::scoped_lock{state_->lock};
    return state_->sending_bytes;
  }

 private:
  std::shared_ptr<CallbackState> state_{std::make_shared<CallbackState>()};
  nw_connection_t connection_{};
};
}  // namespace apple_websocket_backend_internal

std::unique_ptr<IWebSocketBackend> MakeWebSocketBackend(
    std::string const& url) {
  if (__builtin_available(macOS 10.15, iOS 13.0, tvOS 13.0, watchOS 6.0, *)) {
    return std::make_unique<apple_websocket_backend_internal::Backend>(url);
  }
  return nullptr;
}
}  // namespace ae
#endif

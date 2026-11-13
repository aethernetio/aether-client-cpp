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

#ifndef AETHER_TRANSPORT_WEBSOCKET_WEBSOCKET_BACKEND_H_
#define AETHER_TRANSPORT_WEBSOCKET_WEBSOCKET_BACKEND_H_

#include <memory>
#include <string>
#include <vector>

#include "aether/types/data_buffer.h"

namespace ae {
// Platform I/O owns its callbacks and joins/removes them before destruction.
// Only Poll/Send are called by the Aether model thread. No domain/event objects
// are accessed by the platform's receive thread.
class IWebSocketBackend {
 public:
  enum class State { kConnecting, kOpen, kClosed, kError };
  struct Update {
    State state{State::kConnecting};
    std::vector<DataBuffer> received;
  };
  virtual ~IWebSocketBackend() = default;
  virtual Update Poll() = 0;
  virtual bool Send(DataBuffer const& framed_data) = 0;
  virtual std::size_t BufferedAmount() const = 0;
};

std::unique_ptr<IWebSocketBackend> MakeWebSocketBackend(std::string const& url);
}  // namespace ae
#endif

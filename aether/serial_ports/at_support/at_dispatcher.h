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

#ifndef AETHER_SERIAL_PORTS_AT_SUPPORT_AT_DISPATCHER_H_
#define AETHER_SERIAL_PORTS_AT_SUPPORT_AT_DISPATCHER_H_

#include <map>
#include <string>

#include "aether/events/event_subscription.h"
#include "aether/serial_ports/at_support/at_buffer.h"

namespace ae {
class IAtObserver {
 public:
  // never own the pointer to IAtObserver
  // virtual ~IAtObserver() = default;

  // The dispatcher found the required command in buffer
  virtual void Observe(AtBuffer& buffer, AtBuffer::iterator pos) = 0;
};

class AtDispatcher {
 public:
  explicit AtDispatcher(AtBuffer& buffer);

  void Listen(std::string command, IAtObserver* observer);
  void Remove(IAtObserver* observer);

 private:
  void BufferUpdate(AtBuffer::iterator pos);

  AtBuffer* buffer_;
  std::map<std::string, IAtObserver*> observers_;
  Subscription buffer_sub_;
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_DISPATCHER_H_

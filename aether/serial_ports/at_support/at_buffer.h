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

#ifndef AETHER_AT_SUPPORT_AT_BUFFER_H_
#define AETHER_AT_SUPPORT_AT_BUFFER_H_

#include <list>
#include <cstddef>
#include <string_view>

#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/events/event_subscription.h"

#include "aether/serial_ports/iserial_port.h"

namespace ae {
class AtBuffer {
 public:
  using DataLines = std::list<DataBuffer>;
  using iterator = DataLines::iterator;
  using UpdateEvent = Event<void(iterator iters)>;

  explicit AtBuffer(ISerialPort& serial_port);

  UpdateEvent::Subscriber update_event();

  iterator FindPattern(std::string_view str);
  iterator FindPattern(std::string_view str, iterator start);

  DataBuffer GetCrate(std::size_t size, std::size_t offset = 0);
  DataBuffer GetCrate(std::size_t size, std::size_t offset, iterator start);

  iterator begin();
  iterator end();
  iterator erase(iterator it);
  iterator erase(iterator first, iterator last);

 private:
  void DataRead(DataBuffer const& data);

  std::list<DataBuffer> data_lines_;
  UpdateEvent update_event_;
  Subscription data_read_sub_;
};
}  // namespace ae
#endif  // AETHER_AT_SUPPORT_AT_BUFFER_H_

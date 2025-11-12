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

#ifndef AETHER_SERIAL_PORTS_AT_SUPPORT_AT_LISTENER_H_
#define AETHER_SERIAL_PORTS_AT_SUPPORT_AT_LISTENER_H_

#include "aether/types/small_function.h"
#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"

namespace ae {
class AtListener final : public IAtObserver {
 public:
  using Handler = SmallFunction<void(AtBuffer& buffer, AtBuffer::iterator pos)>;

  AtListener(AtDispatcher& dispatcher, std::string expected, Handler handler);
  ~AtListener();

  void Observe(AtBuffer& buffer, AtBuffer::iterator pos) override;

 private:
  AtDispatcher* dispatcher_;
  Handler handler_;
};
}  // namespace ae

#endif  // AETHER_SERIAL_PORTS_AT_SUPPORT_AT_LISTENER_H_

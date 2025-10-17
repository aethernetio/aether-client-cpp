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

#include "aether/serial_ports/at_support/at_listener.h"

#include "aether/tele/tele.h"

namespace ae {

AtListener::AtListener(AtDispatcher& dispatcher, std::string expected,
                       Handler handler)
    : dispatcher_{&dispatcher}, handler_{std::move(handler)} {
  dispatcher_->Listen(std::move(expected), this);
}

AtListener::~AtListener() { dispatcher_->Remove(this); }

void AtListener::Observe(AtBuffer& buffer, AtBuffer::iterator pos) {
  AE_TELED_DEBUG("Listener have heard");
  handler_(buffer, pos);
}
}  // namespace ae

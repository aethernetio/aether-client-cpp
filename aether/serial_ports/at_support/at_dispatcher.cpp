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

#include "aether/serial_ports/at_support/at_dispatcher.h"

#include <cassert>
#include <utility>

namespace ae {
AtDispatcher::AtDispatcher(AtBuffer& buffer)
    : buffer_{&buffer},
      buffer_sub_{buffer_->update_event().Subscribe(
          *this, MethodPtr<&AtDispatcher::BufferUpdate>{})} {}

void AtDispatcher::Listen(std::string command, IAtObserver* observer) {
  auto [_, ok] = observers_.emplace(std::move(command), observer);
  // for one command should be only one observer
  assert(ok);
}

void AtDispatcher::Remove(IAtObserver* observer) {
  for (auto it = std::begin(observers_); it != std::end(observers_);) {
    if (it->second == observer) {
      it = observers_.erase(it);
    } else {
      ++it;
    }
  }
}

void AtDispatcher::BufferUpdate(AtBuffer::iterator pos) {
  for (auto const& [command, observer] : observers_) {
    auto res = buffer_->FindPattern(command, pos);
    if (res != buffer_->end()) {
      observer->Observe(*buffer_, res);
      break;
    }
  }
  // clean the buffer
  buffer_->erase(buffer_->begin(), pos);
}

}  // namespace ae

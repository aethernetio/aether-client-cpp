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
#include <vector>

namespace ae {
AtDispatcher::AtDispatcher(AtBuffer& buffer)
    : buffer_{&buffer},
      buffer_sub_{buffer_->update_event().Subscribe(
          MethodPtr<&AtDispatcher::BufferUpdate>{this})} {}

void AtDispatcher::Listen(std::string command, IAtObserver* observer) {
  // there should be only one observer for one command use last
  observers_.insert_or_assign(
      std::move(command), ObserverEntry{observer, next_generation_++});
}

void AtDispatcher::Remove(IAtObserver* observer) {
  for (auto& [_, entry] : observers_) {
    if (entry.observer == observer) {
      entry.observer = nullptr;
    }
  }
  // remove_guard used to prevent observers_ modification during
  // BufferUpdate(
  if (!remove_guard_) {
    CleanupObservers();
  }
}

void AtDispatcher::BufferUpdate(AtBuffer::iterator pos) {
  remove_guard_ = true;
  // A callback may remove its listener and synchronously install another one
  // for the same command.  Only listeners that existed when this buffer update
  // started are allowed to observe its data.
  auto snapshot = std::vector<std::pair<std::string, ObserverEntry>>{};
  snapshot.reserve(observers_.size());
  for (auto const& entry : observers_) {
    snapshot.push_back(entry);
  }

  for (auto const& [command, entry] : snapshot) {
    if (entry.observer == nullptr) {
      continue;
    }
    auto search = pos;
    while (search != buffer_->end()) {
      auto current = observers_.find(command);
      if (current == observers_.end() ||
          current->second.observer != entry.observer ||
          current->second.generation != entry.generation) {
        break;
      }
      auto res = buffer_->FindPattern(command, search);
      if (res == buffer_->end()) {
        break;
      }
      entry.observer->Observe(*buffer_, res);
      search = ++res;
    }
  }
  // clean the buffer
  buffer_->erase(buffer_->begin(), pos);
  CleanupObservers();
}

void AtDispatcher::CleanupObservers() {
  remove_guard_ = false;
  std::erase_if(observers_, [](auto const& co) {
    return co.second.observer == nullptr;
  });
}

}  // namespace ae

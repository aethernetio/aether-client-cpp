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

#ifndef AETHER_EVENTS_EVENT_IMPL_H_
#define AETHER_EVENTS_EVENT_IMPL_H_

#include <vector>
#include <cstddef>
#include <algorithm>

#include "aether/ptr/rc_ptr.h"

#include "aether/events/event_handler.h"

namespace ae {
struct NoLockSyncPolicy {
  template <typename TFunc>
  decltype(auto) InLock(TFunc&& func) {
    return std::forward<TFunc>(func)();
  }
};

template <typename Signature, typename TSyncPolicy>
class EventImpl;

template <typename... TArgs, typename TSyncPolicy>
class EventImpl<void(TArgs...), TSyncPolicy> {
 public:
  using CallbackSignature = void(TArgs...);

  explicit EventImpl(TSyncPolicy sync_policy)
      : sync_policy_{std::forward<TSyncPolicy>(sync_policy)} {}
  ~EventImpl() = default;

  // store self to prevent removing while emit
  static void Emit(RcPtr<EventImpl> self_ptr, TArgs&&... args) {
    // TODO: find a better way to invoke all handlers
    /*
     * invoke_list is using to prevent iterator invalidation while new
     * handlers added during invocation and recursive event emit.
     */
    auto invoke_list =
        self_ptr->sync_policy_.InLock([&]() { return self_ptr->handlers_; });

    for (auto const& handler : invoke_list) {
      if (handler.is_alive()) {
        // invoke subscription handler
        handler.Invoke(std::forward<TArgs>(args)...);
      }
    }
  }

  // return index of added handler
  std::size_t Add(EventHandler<CallbackSignature>&& handler) {
    return sync_policy_.InLock([&]() {
      auto index = handlers_.size();
      handlers_.emplace_back(std::move(handler));
      return index;
    });
  }

  void Remove(std::size_t index) {
    sync_policy_.InLock([&]() {
      if (index >= handlers_.size()) {
        return;
      }
      handlers_[index].set_alive(false);

      // clean up dead handlers
      // remove only from the end
      auto remove_begin = std::end(handlers_);
      auto it = std::find_if(std::rbegin(handlers_), std::rend(handlers_),
                             [](auto const& h) { return h.is_alive(); });
      if (it != std::rend(handlers_)) {
        remove_begin = it.base();
      }
      handlers_.erase(remove_begin, std::end(handlers_));
    });
  }

 private:
  TSyncPolicy sync_policy_;
  std::vector<EventHandler<CallbackSignature>> handlers_;
};
}  // namespace ae
#endif  // AETHER_EVENTS_EVENT_IMPL_H_

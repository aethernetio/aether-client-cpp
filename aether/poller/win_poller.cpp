/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/poller/win_poller.h"

#if defined WIN_POLLER_ENABLED

#  include <cassert>
#  include <utility>
#  include <map>
#  include <vector>
#  include <thread>
#  include <mutex>
#  include <algorithm>
#  include <atomic>

#  include "aether/tele/tele.h"

namespace ae {
class WinPoller::IoCPPoller {
  struct EventData {
    PollerEvent event;
    IPoller::Callback callback;
  };

  using EventDataList = std::vector<EventData>;

 public:
  IoCPPoller() {
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (iocp_ == nullptr) {
      iocp_ = INVALID_HANDLE_VALUE;
      auto err_code = GetLastError();
      AE_TELED_ERROR("Create iocp error {}", err_code);
      assert(false);
      return;
    }

    loop_thread_ = std::thread{&IoCPPoller::Loop, this};
  }

  ~IoCPPoller() {
    stop_requested_ = true;
    if (iocp_ != INVALID_HANDLE_VALUE) {
      // wake up loop
      PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);

      if (loop_thread_.joinable()) {
        loop_thread_.join();
      }
      CloseHandle(iocp_);
    }
  }

  void Add(PollerEvent event, IPoller::Callback callback) {
    assert(iocp_ != INVALID_HANDLE_VALUE);

    auto lock = std::lock_guard{events_lock_};

    auto comp_key = static_cast<HANDLE>(event.descriptor);
    auto it = events_.find(comp_key);
    if (it == events_.end()) {
      auto res = CreateIoCompletionPort(
          event.descriptor, iocp_, reinterpret_cast<ULONG_PTR>(comp_key), 0);
      if (res == nullptr) {
        AE_TELED_ERROR("Add descriptor to completion port error {}",
                       GetLastError());
        assert(false);
        return;
      }
      auto [new_it, _] = events_.emplace(comp_key, EventDataList{});
      it = new_it;
    }

    // update event list
    it->second.emplace_back(EventData{std::move(event), std::move(callback)});
  }

  void Remove(PollerEvent event) {
    auto lock = std::lock_guard{events_lock_};

    auto comp_key = static_cast<HANDLE>(event.descriptor);
    auto it = events_.find(comp_key);
    if (it == events_.end()) {
      return;
    }
    // remove current event
    auto event_it = std::find_if(
        std::begin(it->second), std::end(it->second),
        [ev{event}](auto const& v) {
          return (v.event.descriptor.descriptor == ev.descriptor.descriptor) &&
                 (v.event.event_type == ev.event_type);
        });
    if (event_it != std::end(it->second)) {
      it->second.erase(event_it);
    }

    // remove whole events entry
    if (it->second.empty()) {
      events_.erase(it);
    }
  }

 private:
  void Loop() {
    assert(iocp_ != INVALID_HANDLE_VALUE);
    while (!stop_requested_) {
      DWORD bytes_transfered;
      ULONG_PTR completion_key;
      LPOVERLAPPED overlapped;
      if (!GetQueuedCompletionStatus(iocp_, &bytes_transfered, &completion_key,
                                     &overlapped, INFINITE)) {
        AE_TELED_DEBUG("GetQueuedCompletionStatus error {}", GetLastError());
      }

      auto lock = std::lock_guard{events_lock_};
      auto it = events_.find(reinterpret_cast<HANDLE>(completion_key));
      if (it == std::end(events_)) {
        continue;
      }
      assert(overlapped);

      auto event_overlapped =
          reinterpret_cast<WinPollerOverlapped*>(overlapped);
      auto event_type = event_overlapped->event_type;

      auto event_it = std::find_if(
          std::begin(it->second), std::end(it->second),
          [=](auto const& v) { return v.event.event_type == event_type; });
      if (event_it == std::end(it->second)) {
        AE_TELED_ERROR("Got not the event we are waiting for");
        assert(false);
      }

      event_it->callback(event_it->event);
    }
  }

  HANDLE iocp_ = INVALID_HANDLE_VALUE;
  std::map<HANDLE, EventDataList> events_;
  std::mutex events_lock_;
  std::thread loop_thread_;
  std::atomic_bool stop_requested_{false};
};

WinPoller::WinPoller() = default;

#  if defined AE_DISTILLATION
WinPoller::WinPoller(Domain* domain) : IPoller(domain) {}
#  endif

WinPoller::~WinPoller() = default;

void WinPoller::Add(PollerEvent event, Callback callback) {
  if (!iocp_poller_) {
    iocp_poller_ = std::make_unique<IoCPPoller>();
  }
  iocp_poller_->Add(event, callback);
}

void WinPoller::Remove(PollerEvent event) {
  assert(iocp_poller_);
  iocp_poller_->Remove(event);
}

}  // namespace ae
#endif

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

#  include <set>
#  include <mutex>
#  include <atomic>
#  include <vector>
#  include <thread>
#  include <cassert>
#  include <utility>
#  include <algorithm>

#  include "aether/tele/tele.h"

namespace ae {
class WinPoller::IoCPPoller {
 public:
  IoCPPoller() {
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (iocp_ == nullptr) {
      iocp_ = INVALID_HANDLE_VALUE;
      AE_TELED_ERROR("Create iocp error {}", GetLastError());
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

  WinPoller::OnPollEventSubscriber Add(DescriptorType descriptor) {
    assert(iocp_ != INVALID_HANDLE_VALUE);

    auto lock = std::unique_lock{events_lock_};

    auto comp_key = static_cast<HANDLE>(descriptor);
    auto [it, inserted] = events_.insert(comp_key);
    if (inserted) {
      auto res = CreateIoCompletionPort(
          descriptor, iocp_, reinterpret_cast<ULONG_PTR>(comp_key), 0);
      if (res == nullptr) {
        AE_TELED_ERROR("Add descriptor to completion port error {}",
                       GetLastError());
        assert(false);
      }
    }
    return WinPoller::OnPollEventSubscriber{poll_event_, std::move(lock)};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard{events_lock_};
    auto comp_key = static_cast<HANDLE>(descriptor);
    events_.erase(comp_key);
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
        auto error = GetLastError();
        if (error == ERROR_CONNECTION_ABORTED) {
          // socket closed
          events_.erase(reinterpret_cast<HANDLE>(completion_key));
          continue;
        }
        AE_TELED_DEBUG("GetQueuedCompletionStatus error {}", error);
      }

      auto lock = std::lock_guard{events_lock_};
      auto descriptor = reinterpret_cast<HANDLE>(completion_key);
      auto it = events_.find(descriptor);
      if (it == std::end(events_)) {
        continue;
      }
      assert(overlapped);

      auto event_overlapped =
          reinterpret_cast<WinPollerOverlapped*>(overlapped);
      poll_event_.Emit(PollerEvent{descriptor, event_overlapped->event_type});
    }
  }

  HANDLE iocp_ = INVALID_HANDLE_VALUE;
  std::set<HANDLE> events_;
  std::recursive_mutex events_lock_;
  std::thread loop_thread_;
  std::atomic_bool stop_requested_{false};
  WinPoller::OnPollEvent poll_event_;
};

WinPoller::WinPoller() = default;

#  if defined AE_DISTILLATION
WinPoller::WinPoller(Domain* domain) : IPoller(domain) {}
#  endif

WinPoller::~WinPoller() = default;

WinPoller::OnPollEventSubscriber WinPoller::Add(DescriptorType descriptor) {
  if (!iocp_poller_) {
    iocp_poller_ = std::make_unique<IoCPPoller>();
  }
  return iocp_poller_->Add(descriptor);
}

void WinPoller::Remove(DescriptorType descriptor) {
  assert(iocp_poller_);
  iocp_poller_->Remove(descriptor);
}

}  // namespace ae
#endif

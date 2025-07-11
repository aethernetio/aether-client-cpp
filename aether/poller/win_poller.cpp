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

#  include "aether/poller/poller_tele.h"

namespace ae {
struct InsertedHandle {
  InsertedHandle() = default;
  InsertedHandle(HANDLE h) : handle{h} {};
  bool operator<(InsertedHandle const& other) const {
    return handle < other.handle;
  }

  HANDLE handle = nullptr;
  mutable bool inserted = false;
};

class WinPoller::IoCPPoller {
 public:
  IoCPPoller() : poll_event_{SharedMutexSyncPolicy{events_lock_}} {
    AE_TELE_INFO(kWinpollWorkerCreate);
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (iocp_ == nullptr) {
      iocp_ = INVALID_HANDLE_VALUE;
      AE_TELE_ERROR(kWinpollInitFailed, "Create iocp error {}", GetLastError());
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
    AE_TELE_INFO(kWinpollWorkerDestroyed);
  }

  WinPoller::OnPollEventSubscriber Add(DescriptorType descriptor) {
    assert(iocp_ != INVALID_HANDLE_VALUE);

    auto lock = std::lock_guard{events_lock_};
    AE_TELE_DEBUG(kWinpollAddDescriptor, "Add poller descriptor");

    auto comp_key = static_cast<HANDLE>(descriptor);
    auto [it, inserted] = handles_.insert(InsertedHandle{comp_key});
    if (inserted) {
      auto res = CreateIoCompletionPort(
          descriptor, iocp_, reinterpret_cast<ULONG_PTR>(comp_key), 0);
      if (res == nullptr) {
        AE_TELE_ERROR(kWinpollAddFailed,
                      "Add descriptor to completion port error {}",
                      GetLastError());
        assert(false);
      }
    }
    it->inserted = true;
    return WinPoller::OnPollEventSubscriber{poll_event_};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard{events_lock_};
    AE_TELE_DEBUG(kWinpollRemoveDescriptor, "Remove poller event");
    auto comp_key = static_cast<HANDLE>(descriptor);
    auto it = handles_.find(comp_key);
    if (it != std::end(handles_)) {
      // mark handle not inserted and not remove handle completely to prevent
      // duplicate Add
      it->inserted = false;
    }
  }

 private:
  void Loop() {
    assert(iocp_ != INVALID_HANDLE_VALUE);
    while (!stop_requested_) {
      DWORD bytes_transferred;
      ULONG_PTR completion_key;
      LPOVERLAPPED overlapped;
      if (!GetQueuedCompletionStatus(iocp_, &bytes_transferred, &completion_key,
                                     &overlapped, INFINITE)) {
        auto error = GetLastError();
        if (error == ERROR_CONNECTION_ABORTED) {
          // socket closed
          handles_.erase(reinterpret_cast<HANDLE>(completion_key));
          continue;
        }
        AE_TELE_ERROR(kWinpollWaitFailed, "GetQueuedCompletionStatus error {}",
                      error);
      }

      auto lock = std::lock_guard{events_lock_};
      auto descriptor = reinterpret_cast<HANDLE>(completion_key);
      auto it = handles_.find(descriptor);
      if ((it == std::end(handles_)) || !it->inserted) {
        continue;
      }

      assert(overlapped);
      auto event_overlapped =
          reinterpret_cast<WinPollerOverlapped*>(overlapped);
      poll_event_.Emit(PollerEvent{descriptor, event_overlapped->event_type});
    }
  }

  HANDLE iocp_ = INVALID_HANDLE_VALUE;
  std::set<InsertedHandle> handles_;
  std::recursive_mutex events_lock_;
  WinPoller::OnPollEvent poll_event_;
  std::atomic_bool stop_requested_{false};
  std::thread loop_thread_;
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
  if (!iocp_poller_) {
    return;
  }
  iocp_poller_->Remove(descriptor);
}

}  // namespace ae
#endif

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

#  include "aether/poller/poller_tele.h"

namespace ae {
IoCpPoller::IoCpPoller() {
  AE_TELE_INFO(kWinpollWorkerCreate);
  iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
  if (iocp_ == nullptr) {
    iocp_ = INVALID_HANDLE_VALUE;
    AE_TELE_ERROR(kWinpollInitFailed, "Create iocp error {}", GetLastError());
    assert(false);
    return;
  }

  loop_thread_ = std::thread{&IoCpPoller::Loop, this};
}

IoCpPoller::~IoCpPoller() {
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

void IoCpPoller::Add(DescriptorType descriptor, EventCb cb) {
  assert(iocp_ != INVALID_HANDLE_VALUE);

  auto lock = std::scoped_lock{poller_mutex_};
  AE_TELE_DEBUG(kWinpollAddDescriptor, "Add poller descriptor");

  auto [it, inserted] = event_map_.insert_or_assign(descriptor, std::move(cb));
  if (inserted) {
    auto comp_key = static_cast<HANDLE>(descriptor);
    auto res = CreateIoCompletionPort(comp_key, iocp_,
                                      reinterpret_cast<ULONG_PTR>(comp_key), 0);
    if (res == nullptr) {
      AE_TELE_ERROR(kWinpollAddFailed,
                    "Add descriptor to completion port error {}",
                    GetLastError());
      event_map_.erase(it);
      assert(false);
    }
  }
}

void IoCpPoller::Remove(DescriptorType descriptor) {
  auto lock = std::scoped_lock{poller_mutex_};
  AE_TELE_DEBUG(kWinpollRemoveDescriptor, "Remove poller event");
  event_map_.erase(descriptor);
}

void IoCpPoller::Loop() {
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
        continue;
      }
      AE_TELE_ERROR(kWinpollWaitFailed, "GetQueuedCompletionStatus error {}",
                    error);
      continue;
    }
    if (overlapped == nullptr) {
      continue;
    }

    HANDLE descriptor;
    // check if descriptor still in handles_
    auto lock = std::scoped_lock{poller_mutex_};
    descriptor = reinterpret_cast<HANDLE>(completion_key);
    auto it = event_map_.find(descriptor);
    if (it != event_map_.end()) {
      it->second(overlapped);
    }
  }
}

WinPoller::WinPoller() = default;
WinPoller::WinPoller(Domain* domain) : IPoller(domain) {}
WinPoller::~WinPoller() = default;

NativePoller* WinPoller::Native() {
  if (!impl_) {
    impl_.emplace();
  }
  return static_cast<NativePoller*>(&*impl_);
}
}  // namespace ae
#endif

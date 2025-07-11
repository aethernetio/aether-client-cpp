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

#include "aether/poller/kqueue_poller.h"

#if defined KQUEUE_POLLER_ENABLED

#  include <sys/event.h>
#  include <unistd.h>
#  include <fcntl.h>

#  include <map>
#  include <array>
#  include <mutex>
#  include <thread>
#  include <memory>
#  include <atomic>
#  include <cstring>
#  include <utility>
#  include <cassert>

#  include "aether/poller/poller_tele.h"

namespace ae {

constexpr auto kMaxEvents = 10;

class KqueuePoller::PollerWorker {
 public:
  PollerWorker()
      : kqueue_fd_{InitKqueue()},
        poll_event_{SharedMutexSyncPolicy{ctl_mutex_}},
        thread_{&PollerWorker::Loop, this} {
    if (kqueue_fd_ == -1) {
      return;
    }
    // Registering event to exit from the thread
    struct kevent ev;
    EV_SET(&ev, exit_kqueue_event_, EVFILT_USER, EV_ADD | EV_ONESHOT, 0, 0,
           nullptr);
    auto res = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (res == -1) {
      AE_TELE_ERROR(kKqueueCreateWakeUpFailed,
                    "KQueue user event set error {} {}", errno,
                    strerror(errno));
      assert(false);
    }
    AE_TELE_DEBUG(kKqueueWorkerCreate);
  }

  ~PollerWorker() {
    if (kqueue_fd_ == -1) {
      return;
    }
    stop_requested_ = true;
    // emit user event to wake up thread
    struct kevent ev;
    EV_SET(&ev, exit_kqueue_event_, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    auto res = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (res == -1) {
      AE_TELE_ERROR(kKqueueWakeUpFailed, "Emit user event error {} {}", errno,
                    strerror(errno));
      assert(false);
    }

    if (thread_.joinable()) {
      thread_.join();
    }

    close(kqueue_fd_);
    AE_TELE_DEBUG(kKqueueWorkerDestroyed);
  }

  [[nodiscard]] KqueuePoller::OnPollEventSubscriber Add(
      DescriptorType descriptor) {
    auto lock = std::lock_guard{ctl_mutex_};
    AE_TELE_DEBUG(kKqueueAddDescriptor, "Add event descriptor {}", descriptor);
    std::array<struct kevent, 2> events;
    std::size_t index = 0;
    for (auto filter : {EVFILT_READ, EVFILT_WRITE}) {
      EV_SET(&events[index], descriptor, filter, EV_ADD | EV_CLEAR, 0, 0,
             nullptr);
      ++index;
    }
    auto res =
        kevent(kqueue_fd_, events.data(), events.size(), nullptr, 0, nullptr);
    if (res == -1) {
      AE_TELE_ERROR(kKqueueAddFailed, "Add event with error {} {}", errno,
                    strerror(errno));
      assert(false);
    }
    return KqueuePoller::OnPollEventSubscriber{poll_event_};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard{ctl_mutex_};
    AE_TELE_DEBUG(kKqueueRemoveDescriptor, "Remove event descriptor {}",
                  descriptor);
    std::array<struct kevent, 2> events;
    std::size_t index = 0;
    for (auto filter : {EVFILT_READ, EVFILT_WRITE}) {
      EV_SET(&events[index], descriptor, filter, EV_DELETE, 0, 0, nullptr);
      ++index;
    }
    auto res =
        kevent(kqueue_fd_, events.data(), events.size(), nullptr, 0, nullptr);
    if (res == -1) {
      AE_TELE_ERROR(kKqueueRemoveFailed, "Remove event error {} {}", errno,
                    strerror(errno));
    }
  }

 private:
  static int InitKqueue() {
    auto fd = kqueue();
    if (fd == -1) {
      AE_TELE_ERROR(kKqueueInitFailed, "Failed to kqueue {} {}", errno,
                    strerror(errno));
      assert(false);
    }
    return fd;
  }

  static EventType FilterTypeToEventType(int filter) {
    switch (filter) {
      case EVFILT_READ:
        return EventType::kRead;
      case EVFILT_WRITE:
        return EventType::kWrite;
      case EVFILT_EXCEPT:
        return EventType::kError;
      default:
        AE_TELE_ERROR(kKqueueUnknownType, "Unknown filter value {}", filter);
        assert(false);
        return {};
    }
  }

  void Loop() {
    while (!stop_requested_) {
      std::array<struct kevent, kMaxEvents> events;
      auto num_events =
          kevent(kqueue_fd_, nullptr, 0, events.data(), events.size(), nullptr);
      if (num_events == -1) {
        AE_TELE_ERROR(kKqueueWaitFailed, "Kqueue event wait error {} {}", errno,
                      strerror(errno));
        assert(false);
        return;
      }
      auto lock = std::lock_guard{ctl_mutex_};
      for (std::size_t i = 0; (i < num_events) && (i < kMaxEvents); ++i) {
        auto& ev = events[i];
        if (ev.filter == EVFILT_USER) {
          // user event
          continue;
        }
        poll_event_.Emit(PollerEvent{static_cast<int>(ev.ident),
                                     FilterTypeToEventType(ev.filter)});
      }
    }
  }

  std::atomic_bool stop_requested_{false};
  int kqueue_fd_;
  const int exit_kqueue_event_ = 1;

  std::recursive_mutex ctl_mutex_;
  KqueuePoller::OnPollEvent poll_event_;
  std::thread thread_;
};

KqueuePoller::KqueuePoller() = default;

#  if defined AE_DISTILLATION
KqueuePoller::KqueuePoller(Domain* domain) : IPoller(domain) {}
#  endif

KqueuePoller::~KqueuePoller() = default;

[[nodiscard]] KqueuePoller::OnPollEventSubscriber KqueuePoller::Add(
    DescriptorType descriptor) {
  if (!poller_worker_) {
    InitPollWorker();
  }
  return poller_worker_->Add(descriptor);
}

void KqueuePoller::Remove(DescriptorType descriptor) {
  if (!poller_worker_) {
    return;
  }
  poller_worker_->Remove(descriptor);
}

void KqueuePoller::InitPollWorker() {
  poller_worker_ = std::make_unique<PollerWorker>();
}
}  // namespace ae
#endif

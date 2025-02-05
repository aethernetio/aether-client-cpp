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

#  include "aether/tele/tele.h"

namespace ae {

constexpr auto MAX_EVENTS = 10;

class KqueuePoller::PollerWorker {
  static constexpr auto READ_END = 0;
  static constexpr auto WRITE_END = 1;

  std::atomic_bool stop_requested_{false};
  int kqueue_fd_;
  const int exit_kqueue_event_ = 1;

  std::thread thread_;
  std::mutex ctl_mutex_;

  std::map<int, Callback> callbacks_;

 public:
  PollerWorker()
      : kqueue_fd_{InitKqueue()}, thread_{&PollerWorker::Loop, this} {
    if (kqueue_fd_ == -1) {
      return;
    }
    // Registering event to exit from the thread
    struct kevent ev;
    EV_SET(&ev, exit_kqueue_event_, EVFILT_USER, EV_ADD | EV_ONESHOT, 0, 0,
           nullptr);
    auto r = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (r == -1) {
      AE_TELED_ERROR("KQueue user event set error {} {}", errno,
                     strerror(errno));
      assert(false);
    }
  }

  ~PollerWorker() {
    if (kqueue_fd_ == -1) {
      return;
    }
    stop_requested_ = true;
    // emit user event to wake up thread
    struct kevent ev;
    EV_SET(&ev, exit_kqueue_event_, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    auto r = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (r == -1) {
      AE_TELED_ERROR("Emit user event error {} {}", errno, strerror(errno));
      assert(false);
    }

    if (thread_.joinable()) {
      thread_.join();
    }

    close(kqueue_fd_);
  }

  void Add(PollerEvent event, Callback callback) {
    struct kevent ev;
    switch (event.event_type) {
      case EventType::READ:
        EV_SET(&ev, event.descriptor, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0,
               nullptr);
        break;
      case EventType::WRITE:
        EV_SET(&ev, event.descriptor, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0,
               nullptr);
        break;
      case EventType::ANY:
        Add(PollerEvent{event.descriptor, EventType::READ}, callback);
        Add(PollerEvent{event.descriptor, EventType::WRITE}, callback);
        return;
    }

    auto r = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (r == -1) {
      AE_TELED_ERROR("Add event error {} {}", errno, strerror(errno));
      assert(false);
    }

    auto lock = std::lock_guard{ctl_mutex_};
    callbacks_.emplace(event.descriptor, std::move(callback));
  }

  void Remove(PollerEvent event) {
    struct kevent ev;
    EV_SET(&ev, event.descriptor, 0, EV_DELETE, 0, 0, nullptr);
    auto r = kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
    if (r == -1) {
      AE_TELED_WARNING("Remove event error {} {}", errno, strerror(errno));
    }

    auto lock = std::lock_guard{ctl_mutex_};
    callbacks_.erase(event.descriptor);
  }

 private:
  int InitKqueue() {
    auto fd = kqueue();
    if (fd == -1) {
      AE_TELED_ERROR("Failed to kqueue {} {}", errno, strerror(errno));
      assert(false);
      return -1;
    }
    return fd;
  }

  EventType FilterTypeToEventType(int filter) {
    switch (filter) {
      case EVFILT_READ:
        return EventType::READ;
      case EVFILT_WRITE:
        return EventType::WRITE;
      default:
        AE_TELED_ERROR("Unknown filter value {}", filter);
        assert(false);
        return {};
    }
  }

  void Loop() {
    while (!stop_requested_) {
      std::array<struct kevent, MAX_EVENTS> events;
      auto num_events =
          kevent(kqueue_fd_, nullptr, 0, events.data(), events.size(), nullptr);
      if (num_events == -1) {
        AE_TELED_ERROR("Kqueue event wait error {} {}", errno, strerror(errno));
        assert(false);
        return;
      }
      for (std::size_t i = 0; i < num_events; ++i) {
        auto lock = std::lock_guard{ctl_mutex_};

        auto& ev = events[i];
        if (ev.filter == EVFILT_USER) {
          AE_TELED_DEBUG("Got user event");
          // user event
          continue;
        }
        auto cb_it = callbacks_.find(static_cast<int>(ev.ident));
        if (cb_it == callbacks_.end()) {
          AE_TELED_ERROR("Callback for {} and filter {} not found", ev.ident,
                         ev.filter);
          assert(false);
          return;
        }
        auto& cb = cb_it->second;
        cb(PollerEvent{static_cast<int>(ev.ident),
                       FilterTypeToEventType(ev.filter)});
      }
    }
  }
};

KqueuePoller::KqueuePoller() = default;

#  if defined AE_DISTILLATION
KqueuePoller::KqueuePoller(Domain* domain) : IPoller(domain) {}
#  endif

KqueuePoller::~KqueuePoller() = default;

void KqueuePoller::Add(PollerEvent event, Callback callback) {
  if (!poller_worker_) {
    InitPollWorker();
  }
  poller_worker_->Add(std::move(event), std::move(callback));
}

void KqueuePoller::Remove(PollerEvent event) {
  assert(poller_worker_);
  poller_worker_->Remove(std::move(event));
}

void KqueuePoller::InitPollWorker() {
  poller_worker_ = std::make_unique<PollerWorker>();
}

}  // namespace ae
#endif

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
namespace kqueue_poller_internal {
int FillKQueueFilter(struct kevent* ev, int size, DescriptorType fd,
                     EventType event_type) {
  int index = 0;
  for (auto e : {EventType::kRead, EventType::kWrite}) {
    if (index >= size) {
      break;
    }
    if ((event_type & e) != 0) {
      int filt;
      switch (e) {
        case EventType::kRead:
          filt = EVFILT_READ;
          break;
        case EventType::kWrite:
          filt = EVFILT_WRITE;
          break;
        default:
          continue;
      }
      EV_SET(&ev[index++], fd, filt, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    }
  }
  return index;
}

EventType FilterTypeToEventType(int filter) {
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
      break;
  }
  return {};
}
}  // namespace kqueue_poller_internal

KqueuePollerImpl::KqueuePollerImpl()
    : kqueue_fd_{InitKqueue()}, thread_{&KqueuePollerImpl::Loop, this} {
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
                  "KQueue user event set error {} {}", errno, strerror(errno));
    assert(false);
  }
  AE_TELE_DEBUG(kKqueueWorkerCreate);
}

KqueuePollerImpl::~KqueuePollerImpl() {
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

void KqueuePollerImpl::Event(DescriptorType fd, EventType event, EventCb cb) {
  AE_TELE_DEBUG(kKqueueAddDescriptor, "Add descriptor {} event {}", fd, event);
  auto lock = std::scoped_lock{poller_mutex_};
  event_map_[fd] = std::move(cb);

  std::array<struct kevent, 2> events;
  auto count = kqueue_poller_internal::FillKQueueFilter(
      events.data(), events.size(), fd, event);
  auto res = kevent(kqueue_fd_, events.data(), count, nullptr, 0, nullptr);
  if (res == -1) {
    AE_TELE_ERROR(kKqueueAddFailed, "Add event with error {} {}", errno,
                  strerror(errno));
    assert(false);
  }
}

void KqueuePollerImpl::Remove(DescriptorType fd) {
  AE_TELE_DEBUG(kKqueueRemoveDescriptor, "Remove event descriptor {}", fd);
  auto lock = std::scoped_lock{poller_mutex_};
  event_map_.erase(fd);

  // remove all kind of events
  std::array<struct kevent, 2> events;
  auto count = kqueue_poller_internal::FillKQueueFilter(
      events.data(), events.size(), fd, EventType::kRead | EventType::kWrite);
  auto res = kevent(kqueue_fd_, events.data(), count, nullptr, 0, nullptr);
  if (res == -1) {
    AE_TELE_ERROR(kKqueueRemoveFailed, "Remove event error {} {}", errno,
                  strerror(errno));
  }
}

int KqueuePollerImpl::InitKqueue() {
  auto fd = kqueue();
  if (fd == -1) {
    AE_TELE_ERROR(kKqueueInitFailed, "Failed to kqueue {} {}", errno,
                  strerror(errno));
    assert(false);
  }
  return fd;
}

void KqueuePollerImpl::Loop() {
  constexpr auto kMaxEvents = 10;
  std::array<struct kevent, kMaxEvents> events;

  while (!stop_requested_) {
    auto num_events =
        kevent(kqueue_fd_, nullptr, 0, events.data(), events.size(), nullptr);
    if (num_events == -1) {
      AE_TELE_ERROR(kKqueueWaitFailed, "Kqueue event wait error {} {}", errno,
                    strerror(errno));
      assert(false);
      return;
    }

    auto lock = std::scoped_lock{poller_mutex_};

    for (std::size_t i = 0; (i < num_events) && (i < kMaxEvents); ++i) {
      auto& ev = events[i];
      if (ev.filter == EVFILT_USER) {
        // user event
        continue;
      }
      auto poller_event =
          event_map_.find(DescriptorType{static_cast<int>(ev.ident)});
      if (poller_event == event_map_.end()) {
        continue;
      }
      poller_event->second(
          kqueue_poller_internal::FilterTypeToEventType(ev.filter));
    }
  }
}

KqueuePoller::KqueuePoller() = default;

KqueuePoller::KqueuePoller(ObjProp prop) : IPoller{prop} {}

NativePoller* KqueuePoller::Native() {
  if (!impl_) {
    impl_.emplace();
  }
  return static_cast<NativePoller*>(&*impl_);
}

}  // namespace ae
#endif

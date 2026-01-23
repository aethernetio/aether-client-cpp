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

#include "aether/poller/epoll_poller.h"

#if defined EPOLL_POLLER_ENABLED

#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/epoll.h>
#  include <sys/eventfd.h>

#  include <array>
#  include <cerrno>
#  include <cstring>

#  include "aether/poller/poller_tele.h"

namespace ae {
namespace epoll_poller_internal {
std::uint32_t PollerEventsToEpol(EventType event) {
  std::uint32_t event_flags = 0;
  if ((event & EventType::kRead) != 0) {
    event_flags |= EPOLLIN;
  }
  if ((event & EventType::kWrite) != 0) {
    event_flags |= EPOLLOUT;
  }
  if ((event & EventType::kError) != 0) {
    event_flags |= EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP;
  }
  return event_flags;
}

EventType EpollEventToEventType(std::uint32_t event_flags) {
  EventType event{};
  if ((event_flags & EPOLLIN) != 0) {
    event |= EventType::kRead;
  }
  if ((event_flags & EPOLLOUT) != 0) {
    event |= EventType::kWrite;
  }
  if ((event_flags & (EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP)) != 0) {
    event |= EventType::kError;
  }
  return static_cast<EventType>(event);
}

}  // namespace epoll_poller_internal

EpollImpl::EpollImpl()
    : epoll_fd_{InitEpoll()},
      event_fd_{MakeEventFd()},
      thread_(&EpollImpl::Loop, this) {
  AE_TELE_INFO(kEpollWorkerCreate);

  // add wake up fd to epoll
  if (event_fd_ != -1) {
    Event(DescriptorType{event_fd_}, EventType::kRead, [](auto) {});
  }
}

EpollImpl::~EpollImpl() {
  AE_TELED_DEBUG("Destroy EpollImpl event fd {}, thread is joinable {}",
                 event_fd_, thread_.joinable());

  stop_requested_ = true;
  if (event_fd_ != -1) {
    // write something
    std::uint64_t b{1};
    auto res = write(event_fd_, &b, sizeof(b));
    if (res != sizeof(b)) {
      AE_TELED_ERROR("Failed to write to event fd {} {}", errno,
                     strerror(errno));
    }
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  if (epoll_fd_ != -1) {
    close(epoll_fd_);
  }
  if (event_fd_ != -1) {
    close(event_fd_);
  }
  AE_TELE_INFO(kEpollWorkerDestroyed);
}

void EpollImpl::Event(DescriptorType fd, EventType event, EventCb cb) {
  AE_TELE_DEBUG(kEpollAddDescriptor, "Poller event fd:{} event:{}", fd, event);
  struct epoll_event epoll_event;
  epoll_event.events = epoll_poller_internal::PollerEventsToEpol(event);
  // watch only edge triggered events
  epoll_event.events |= EPOLLET;
  epoll_event.data.fd = fd;

  auto lock = std::scoped_lock{poller_mutex_};
  auto [_, new_event] = event_map_.insert_or_assign(fd, std::move(cb));
  int op = new_event ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
  auto res = epoll_ctl(epoll_fd_, op, fd, &epoll_event);
  if (res < 0) {
    AE_TELE_ERROR(kEpollAddFailed, "Failed to add to epoll {} {}", errno,
                  strerror(errno));
    assert(false);
  }
}

void EpollImpl::Remove(DescriptorType fd) {
  AE_TELE_DEBUG(kEpollRemoveDescriptor, "Remove poller event {}", fd);

  auto lock = std::scoped_lock{poller_mutex_};
  event_map_.erase(fd);

  struct epoll_event epoll_event{};
  auto res = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &epoll_event);
  if (res < 0) {
    if (errno != ENOENT) {
      AE_TELE_ERROR(kEpollRemoveFailed, "Failed to remove from epoll {} {}",
                    errno, strerror(errno));
      assert(false);
    }
  }
}

int EpollImpl::MakeEventFd() {
  auto fd = eventfd(0, EFD_CLOEXEC);
  // create pipe to wake up the thread
  if (fd < 0) {
    AE_TELE_ERROR(kEpollCreateWakeUpFailed,
                  "Failed to create wake up pipe {} {}", errno,
                  strerror(errno));
    assert(false);
    return -1;
  }
  return fd;
}

int EpollImpl::InitEpoll() {
  auto fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    AE_TELE_ERROR(kEpollInitFailed, "Failed to create epoll fd {} {}", errno,
                  strerror(errno));
    assert(false);
  }
  return fd;
}

void EpollImpl::Loop() {
  static constexpr auto kMaxEvents = 10;
  std::array<struct epoll_event, kMaxEvents> events;

  while (!stop_requested_) {
    auto res = epoll_wait(epoll_fd_, events.data(), events.size(), -1);
    if (res < 0) {
      if (errno == EINTR) {
        continue;
      }
      AE_TELE_ERROR(kEpollWaitFailed, "Failed to epoll_wait {} {}", errno,
                    strerror(errno));
      assert(false);
      continue;
    }

    auto lock = std::scoped_lock{poller_mutex_};

    for (std::size_t i = 0;
         (i < static_cast<std::size_t>(res)) && (i < kMaxEvents); ++i) {
      auto& event = events[i];
      auto fd = event.data.fd;
      if (fd == event_fd_) {
        continue;
      }
      auto poller_event = event_map_.find(fd);
      if (poller_event == event_map_.end()) {
        continue;
      }
      auto ev_type = epoll_poller_internal::EpollEventToEventType(event.events);
      poller_event->second(ev_type);
    }
  }
}

EpollPoller::EpollPoller() = default;

EpollPoller::EpollPoller(ObjProp prop) : IPoller{prop} {}

NativePoller* EpollPoller::Native() {
  if (!impl_) {
    impl_.emplace();
  }
  return static_cast<NativePoller*>(&*impl_);
}

}  // namespace ae
#endif

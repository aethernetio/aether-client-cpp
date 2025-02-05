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

#  include <sys/epoll.h>
#  include <unistd.h>
#  include <fcntl.h>

#  include <map>
#  include <array>
#  include <mutex>
#  include <atomic>
#  include <thread>
#  include <utility>
#  include <cerrno>
#  include <cstring>

#  include "aether/tele/tele.h"

namespace ae {

constexpr auto MAX_EVENTS = 10;

class EpollPoller::PollWorker {
  static constexpr auto READ_END = 0;
  static constexpr auto WRITE_END = 1;
  std::atomic_bool stop_requested_{false};
  std::array<int, 2> wake_up_pipe_;
  int epoll_fd_;

  std::thread thread_;
  std::mutex ctl_mutex_;

  std::map<int, Callback> callbacks_;

 public:
  PollWorker()
      : wake_up_pipe_{WakeUpPipe()},
        epoll_fd_{InitEpoll()},
        thread_(&PollWorker::Loop, this) {
    // add wake up pipe to epoll
    Add(PollerEvent{wake_up_pipe_[READ_END], EventType::READ}, [](auto event) {
      AE_TELED_DEBUG("Wake up pipe read {} type {}",
                     static_cast<int>(event.descriptor), event.event_type);
      // empty pipe
      char buf[1];
      [[maybe_unused]] auto r = read(event.descriptor, buf, 1);
    });
  }

  ~PollWorker() {
    AE_TELED_DEBUG("Destroy PollWorker WRITE PIPE {}, thread is joinable {}",
                   wake_up_pipe_[WRITE_END], thread_.joinable());

    stop_requested_ = true;
    if (wake_up_pipe_[WRITE_END] != -1) {
      // write something
      [[maybe_unused]] auto r = write(wake_up_pipe_[WRITE_END], "", 1);
      close(wake_up_pipe_[WRITE_END]);
    }

    if (thread_.joinable()) {
      thread_.join();
    }

    if (epoll_fd_ != -1) {
      close(epoll_fd_);
    }
    if (wake_up_pipe_[READ_END] != -1) {
      close(wake_up_pipe_[READ_END]);
    }
    AE_TELED_DEBUG("Poll worker destroyed");
  }

  void Add(PollerEvent event, Callback callback) {
    auto lock = std::lock_guard(ctl_mutex_);
    struct epoll_event epoll_event;
    switch (event.event_type) {
      case EventType::READ:
        epoll_event.events = EPOLLIN;
        break;
      case EventType::WRITE:
        epoll_event.events = EPOLLOUT;
        break;
      case EventType::ANY:
        epoll_event.events = EPOLLIN | EPOLLOUT;
        break;
    }

    // watch only edge triggered events
    epoll_event.events |= EPOLLET;
    epoll_event.data.fd = event.descriptor;

    auto r =
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event.descriptor, &epoll_event);
    if (r < 0) {
      AE_TELED_ERROR("Failed to add to epoll {} {}", errno, strerror(errno));
      assert(false);
    }
    callbacks_.emplace(event.descriptor, std::move(callback));
  }

  void Remove(PollerEvent event) {
    auto lock = std::lock_guard(ctl_mutex_);
    auto it = callbacks_.find(event.descriptor);
    if (it != callbacks_.end()) {
      callbacks_.erase(it);
    }

    struct epoll_event epoll_event{};

    auto r =
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, event.descriptor, &epoll_event);
    if (r < 0) {
      AE_TELED_ERROR("Failed to remove from epoll {} {}", errno,
                     strerror(errno));
      assert(false);
    }
  }

 private:
  EventType FromEpollEvent(uint32_t events) {
    if (events == EPOLLIN) {
      return EventType::READ;
    }
    if (events == EPOLLOUT) {
      return EventType::WRITE;
    }
    return EventType::ANY;
  }

  std::array<int, 2> WakeUpPipe() {
    int pipes[2];
    auto r = pipe2(pipes, O_CLOEXEC);
    // create pipe used to wake up the thread
    if (r < 0) {
      AE_TELED_ERROR("Failed to create wake up pipe {} {}", errno,
                     strerror(errno));
      assert(false);
      return {-1, -1};
    }
    return {pipes[0], pipes[1]};
  }

  int InitEpoll() {
    auto fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
      AE_TELED_ERROR("Failed to create epoll fd {} {}", errno, strerror(errno));
      assert(false);
    }
    return fd;
  }

  void Loop() {
    while (!stop_requested_) {
      std::array<struct epoll_event, MAX_EVENTS> events;
      auto r = epoll_wait(epoll_fd_, events.data(), events.size(), -1);
      if (r < 0) {
        if (errno == EINTR) {
          continue;
        }
        AE_TELED_ERROR("Failed to epoll_wait {} {}", errno, strerror(errno));
        assert(false);
        continue;
      }

      auto lock = std::lock_guard(ctl_mutex_);

      for (std::size_t i = 0; i < static_cast<std::size_t>(r); ++i) {
        auto& event = events[i];
        auto fd = event.data.fd;
        auto cb_it = callbacks_.find(fd);
        if (cb_it == callbacks_.end()) {
          AE_TELED_ERROR("Failed to find callback for fd {}", fd);
          assert(false);
          continue;
        }
        auto& cb = cb_it->second;
        cb(PollerEvent{event.data.fd, FromEpollEvent(event.events)});
      }
    }
  }
};

EpollPoller::EpollPoller() = default;

#  if defined AE_DISTILLATION
EpollPoller::EpollPoller(Domain* domain) : IPoller(domain) {}
#  endif

EpollPoller::~EpollPoller() = default;

void EpollPoller::Add(PollerEvent event, Callback callback) {
  if (!poll_worker_) {
    InitPollWorker();
  }
  poll_worker_->Add(event, std::move(callback));
}

void EpollPoller::Remove(PollerEvent event) {
  assert(poll_worker_);
  poll_worker_->Remove(event);
}

void EpollPoller::InitPollWorker() {
  poll_worker_ = std::make_unique<PollWorker>();
}

}  // namespace ae

#endif

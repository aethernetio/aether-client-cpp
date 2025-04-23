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

#  include <array>
#  include <mutex>
#  include <atomic>
#  include <thread>
#  include <cerrno>
#  include <cstring>

#  include "aether/events/events.h"

#  include "aether/poller/poller_tele.h"

namespace ae {

inline constexpr auto kMaxEvents = 10;

class EpollPoller::PollWorker {
  static constexpr auto kPipeReadEnd = 0;
  static constexpr auto kPipeWriteEnd = 1;

 public:
  PollWorker()
      : wake_up_pipe_{WakeUpPipe()},
        epoll_fd_{InitEpoll()},
        poll_event_{SharedMutexSyncPolicy{ctl_mutex_}},
        thread_(&PollWorker::Loop, this) {
    AE_TELE_INFO(PollerWorkerCreate);
    // add wake up pipe to epoll
    wake_up_subscription_ =
        Add(DescriptorType{wake_up_pipe_[kPipeReadEnd]})
            .Subscribe(*this, MethodPtr<&PollWorker::EmptyWakeUpPipe>{});
  }

  ~PollWorker() {
    AE_TELED_DEBUG("Destroy PollWorker WRITE PIPE {}, thread is joinable {}",
                   wake_up_pipe_[kPipeWriteEnd], thread_.joinable());

    stop_requested_ = true;
    if (wake_up_pipe_[kPipeWriteEnd] != -1) {
      // write something
      [[maybe_unused]] auto res = write(wake_up_pipe_[kPipeWriteEnd], "", 1);
      close(wake_up_pipe_[kPipeWriteEnd]);
    }

    if (thread_.joinable()) {
      thread_.join();
    }

    if (epoll_fd_ != -1) {
      close(epoll_fd_);
    }
    if (wake_up_pipe_[kPipeReadEnd] != -1) {
      close(wake_up_pipe_[kPipeReadEnd]);
    }
    AE_TELE_INFO(PollerWorkerDestroyed);
  }

  [[nodiscard]] EpollPoller::OnPollEventSubscriber Add(
      DescriptorType descriptor) {
    auto lock = std::lock_guard(ctl_mutex_);

    AE_TELE_DEBUG(PollerAddDescriptor, "Add poller descriptor {}", descriptor);
    struct epoll_event epoll_event;
    // watch in and out
    epoll_event.events = EPOLLIN | EPOLLOUT;
    // watch only edge triggered events
    epoll_event.events |= EPOLLET;
    // watch if socket closed
    epoll_event.events |= EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP;
    epoll_event.data.fd = descriptor;

    auto res = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, descriptor, &epoll_event);
    if (res < 0) {
      AE_TELE_ERROR(PollerAddFailed, "Failed to add to epoll {} {}", errno,
                    strerror(errno));
      assert(false);
    }

    return EpollPoller::OnPollEventSubscriber{poll_event_};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard(ctl_mutex_);

    AE_TELE_DEBUG(PollerRemoveDescriptor, "Remove poller event {}", descriptor);
    struct epoll_event epoll_event{};

    auto res = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, descriptor, &epoll_event);
    if (res < 0) {
      if (errno != ENOENT) {
        AE_TELE_ERROR(PollerRemoveFailed, "Failed to remove from epoll {} {}",
                      errno, strerror(errno));
        assert(false);
      }
    }
  }

 private:
  static std::vector<EventType> FromEpollEvent(std::uint32_t events) {
    auto res = std::vector<EventType>{};
    res.reserve(3);
    if ((events & EPOLLIN) != 0) {
      res.push_back(EventType::kRead);
    }
    if ((events & EPOLLOUT) != 0) {
      res.push_back(EventType::kWrite);
    }
    if ((events & (EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP)) != 0) {
      res.push_back(EventType::kError);
    }
    return res;
  }

  static std::array<int, 2> WakeUpPipe() {
    std::array<int, 2> pipes;
    auto res = pipe2(pipes.data(), O_CLOEXEC);
    // create pipe to wake up the thread
    if (res < 0) {
      AE_TELE_ERROR(PollerCreateWakeUpFailed,
                    "Failed to create wake up pipe {} {}", errno,
                    strerror(errno));
      assert(false);
      return {-1, -1};
    }
    return pipes;
  }

  void EmptyWakeUpPipe(PollerEvent event) {
    if (event.descriptor != wake_up_pipe_[kPipeReadEnd]) {
      return;
    }
    // drain the pipe
    std::array<char, 1> buf;
    [[maybe_unused]] auto res = read(event.descriptor, buf.data(), buf.size());
  }

  static int InitEpoll() {
    auto fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
      AE_TELE_ERROR(PollerInitFailed, "Failed to create epoll fd {} {}", errno,
                    strerror(errno));
      assert(false);
    }
    return fd;
  }

  void Loop() {
    std::array<struct epoll_event, kMaxEvents> events;

    while (!stop_requested_) {
      auto res = epoll_wait(epoll_fd_, events.data(), events.size(), -1);
      if (res < 0) {
        if (errno == EINTR) {
          continue;
        }
        AE_TELE_ERROR(PollerWaitFailed, "Failed to epoll_wait {} {}", errno,
                      strerror(errno));
        assert(false);
        continue;
      }

      auto lock = std::lock_guard(ctl_mutex_);

      for (std::size_t i = 0;
           (i < static_cast<std::size_t>(res)) && (i < kMaxEvents); ++i) {
        auto& event = events[i];
        auto fd = event.data.fd;
        for (auto ev_type : FromEpollEvent(event.events)) {
          poll_event_.Emit(PollerEvent{fd, ev_type});
        }
      }
    }
  }

  std::atomic_bool stop_requested_{false};
  std::array<int, 2> wake_up_pipe_;
  int epoll_fd_;

  std::recursive_mutex ctl_mutex_;

  EpollPoller::OnPollEvent poll_event_;
  Subscription wake_up_subscription_;

  std::thread thread_;
};

EpollPoller::EpollPoller() = default;

#  if defined AE_DISTILLATION
EpollPoller::EpollPoller(Domain* domain) : IPoller(domain) {}
#  endif

EpollPoller::~EpollPoller() = default;

EpollPoller::OnPollEventSubscriber EpollPoller::Add(DescriptorType descriptor) {
  if (!poll_worker_) {
    InitPollWorker();
  }
  return poll_worker_->Add(descriptor);
}

void EpollPoller::Remove(DescriptorType descriptor) {
  assert(poll_worker_);
  poll_worker_->Remove(descriptor);
}

void EpollPoller::InitPollWorker() {
  poll_worker_ = std::make_unique<PollWorker>();
}
}  // namespace ae
#endif

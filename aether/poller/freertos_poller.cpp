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

#include "aether/poller/freertos_poller.h"

#if defined FREERTOS_POLLER_ENABLED

#  include <set>
#  include <mutex>
#  include <atomic>
#  include <vector>
#  include <utility>

#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "lwip/sockets.h"

#  include "aether/poller/poller_tele.h"

namespace ae {
namespace freertos_poller_internal {
inline struct sockaddr_in MakeLoopbackAddr() {
  constexpr char kLoopbackIp[] = "127.0.0.1";
  struct sockaddr_in loopback_addr;
  memset(&loopback_addr, 0, sizeof(loopback_addr));
  loopback_addr.sin_family = AF_INET;
  loopback_addr.sin_addr.s_addr = inet_addr(kLoopbackIp);
  loopback_addr.sin_port = htons(66);
  return loopback_addr;
}

inline int MakeListenPart() {
  int l_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (l_socket == -1) {
    AE_TELED_ERROR("Create listen socket failed with {}", errno);
    assert(false);
    return -1;
  }
  fcntl(l_socket, F_SETFL, O_NONBLOCK);
  auto const addr = MakeLoopbackAddr();
  if (bind(l_socket, reinterpret_cast<sockaddr const *>(&addr), sizeof(addr)) !=
      0) {
    AE_TELED_ERROR("Bind listen socket failed with {}", errno);
    close(l_socket);
    assert(false);
    return -1;
  }
  if (connect(l_socket, reinterpret_cast<sockaddr const *>(&addr),
              sizeof(addr)) != 0) {
    AE_TELED_ERROR("Create  socket failed with {}", errno);
    close(l_socket);
    assert(false);
    return -1;
  }
  return l_socket;
}

inline std::array<int, 2> MakePipe() {
  auto sock = MakeListenPart();
  return {sock, sock};
}

inline void WritePipe(std::array<int, 2> const &pipe) {
  if (pipe[1] != -1) {
    send(pipe[1], "1", 1, 0);
  }
}

inline void ReadPipe(std::array<int, 2> const &pipe) {
  if (pipe[0] != -1) {
    std::uint8_t buff[16];
    auto n = recv(pipe[0], &buff, sizeof(buff), 0);
    (void)(n);
  }
}

inline void ClosePipe(std::array<int, 2> &pipe) {
  if (pipe[1] != -1) {
    close(pipe[1]);
    pipe[1] = -1;
  }
  if (pipe[0] != -1) {
    close(pipe[0]);
    pipe[0] = -1;
  }
}
}  // namespace freertos_poller_internal

void vTaskFunction(void *pvParameters);

class FreertosPoller::PollWorker {
 public:
  PollWorker()
      : wake_up_pipe_{freertos_poller_internal::MakePipe()},
        stop_requested_{false},
        poll_event_{SharedMutexSyncPolicy{ctl_mutex_}} {
    assert(wake_up_pipe_[0] != -1);
    assert(wake_up_pipe_[1] != -1);

    xTaskCreate(static_cast<void (*)(void *)>(&vTaskFunction), "Poller loop",
                4096, static_cast<void *>(this), tskIDLE_PRIORITY,
                &myTaskHandle_);
    AE_TELE_DEBUG(kFreertosWorkerCreate, "Poll worker was created");
  }

  ~PollWorker() {
    stop_requested_ = true;
    freertos_poller_internal::WritePipe(wake_up_pipe_);
    if (myTaskHandle_ != nullptr) {
      vTaskDelete(myTaskHandle_);
    }
    freertos_poller_internal::ClosePipe(wake_up_pipe_);
    AE_TELE_DEBUG(kFreertosWorkerDestroyed, "Poll worker has been destroyed");
  }

  [[nodiscard]] OnPollEventSubscriber Add(DescriptorType descriptor) {
    auto lock = std::lock_guard(ctl_mutex_);
    AE_TELE_DEBUG(kFreertosAddDescriptor, "Added descriptor {}", descriptor);
    freertos_poller_internal::WritePipe(wake_up_pipe_);
    descriptors_.insert(descriptor);
    return OnPollEventSubscriber{poll_event_};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard(ctl_mutex_);
    freertos_poller_internal::WritePipe(wake_up_pipe_);
    descriptors_.erase(descriptor);
    AE_TELE_DEBUG(kFreertosRemoveDescriptor, "Removed descriptor {}",
                  descriptor);
  }

  void Loop(void) {
    static constexpr int kPollingTimeout = 16000;
    int res;

    while (!stop_requested_) {
      {
        auto lock = std::lock_guard{ctl_mutex_};
        fds_vector_ = FillFdsVector(descriptors_);
      }
      res = lwip_poll(fds_vector_.data(), fds_vector_.size(), kPollingTimeout);
      if (res == -1) {
        AE_TELE_ERROR(kFreertosWaitFailed, "Polling error {} {}", errno,
                      strerror(errno));
        continue;
      } else if (res == 0) {
        // timeout
        continue;
      }

      auto lock = std::lock_guard{ctl_mutex_};
      for (auto const &v : fds_vector_) {
        if ((v.fd == wake_up_pipe_[0])) {
          if ((v.revents & POLLIN) != 0) {
            freertos_poller_internal::ReadPipe(wake_up_pipe_);
          }
        } else {
          for (auto event : FromEpollEvent(v.revents)) {
            poll_event_.Emit(PollerEvent{v.fd, event});
          }
        }
      }
    }
  }

 private:
  std::vector<pollfd> FillFdsVector(std::set<int> const &descriptors) {
    std::vector<pollfd> fds_vector_l;
    fds_vector_l.reserve(descriptors_.size() + 1);
    pollfd fds;

    fds.fd = wake_up_pipe_[0];
    fds.events = POLLIN;
    fds.revents = 0;
    fds_vector_l.push_back(fds);

    for (const auto &desc : descriptors) {
      fds.fd = desc;
      fds.events = POLLIN | POLLOUT;
      fds.revents = 0;
      fds_vector_l.push_back(fds);
    }

    return fds_vector_l;
  }

  std::vector<EventType> FromEpollEvent(std::uint32_t events) {
    std::vector<EventType> res;
    res.reserve(3);

    if ((events & POLLIN) != 0) {
      res.push_back(EventType::kRead);
    }
    if ((events & POLLOUT) != 0) {
      res.push_back(EventType::kWrite);
    }
    if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      res.push_back(EventType::kError);
    }
    return res;
  }

  TaskHandle_t myTaskHandle_ = nullptr;
  std::array<int, 2> wake_up_pipe_;
  std::atomic_bool stop_requested_;
  std::vector<pollfd> fds_vector_;
  std::set<int> descriptors_;
  std::recursive_mutex ctl_mutex_;
  OnPollEvent poll_event_;
};

void vTaskFunction(void *pvParameters) {
  FreertosPoller::PollWorker *poller;

  poller = static_cast<FreertosPoller::PollWorker *>(pvParameters);

  poller->Loop();
}

FreertosPoller::FreertosPoller() = default;

#  if defined AE_DISTILLATION
FreertosPoller::FreertosPoller(Domain *domain) : IPoller(domain) {}
#  endif

FreertosPoller::~FreertosPoller() = default;

FreertosPoller::OnPollEventSubscriber FreertosPoller::Add(
    DescriptorType descriptor) {
  if (!poll_worker_) {
    InitPollWorker();
  }
  return poll_worker_->Add(descriptor);
}

void FreertosPoller::Remove(DescriptorType descriptor) {
  if (!poll_worker_) {
    return;
  }
  poll_worker_->Remove(descriptor);
}

void FreertosPoller::InitPollWorker() {
  poll_worker_ = std::make_unique<PollWorker>();
}
}  // namespace ae

#endif

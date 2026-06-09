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

#  include <utility>
#  include <cassert>

#  include "lwip/sockets.h"

#  include "aether/poller/poller_tele.h"

namespace ae {
namespace freertos_poller_internal {
inline struct sockaddr_in MakeLoopbackAddr(std::uint16_t port) noexcept {
  constexpr char kLoopbackIp[] = "127.0.0.1";
  struct sockaddr_in loopback_addr;
  memset(&loopback_addr, 0, sizeof(loopback_addr));
  loopback_addr.sin_family = AF_INET;
  loopback_addr.sin_addr.s_addr = inet_addr(kLoopbackIp);
  loopback_addr.sin_port = htons(port);
  return loopback_addr;
}

inline int MakeLoopbackSocket(std::uint16_t port_listen,
                              std::uint16_t port_connect) noexcept {
  int l_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (l_socket == -1) {
    AE_TELED_ERROR("Create listen socket failed with {}", errno);
    assert(false);
    return -1;
  }
  fcntl(l_socket, F_SETFL, O_NONBLOCK);
  auto const bind_addr = MakeLoopbackAddr(port_listen);
  if (bind(l_socket, reinterpret_cast<sockaddr const*>(&bind_addr),
           sizeof(bind_addr)) != 0) {
    AE_TELED_ERROR("Bind socket failed with {}", errno);
    close(l_socket);
    assert(false);
    return -1;
  }
  auto const conn_addr = MakeLoopbackAddr(port_connect);
  if (connect(l_socket, reinterpret_cast<sockaddr const*>(&conn_addr),
              sizeof(conn_addr)) != 0) {
    AE_TELED_ERROR("Connect socket failed with {}", errno);
    close(l_socket);
    assert(false);
    return -1;
  }
  return l_socket;
}

inline int MakeListenPart() noexcept { return MakeLoopbackSocket(66, 67); }
inline int MakeWritePart() noexcept { return MakeLoopbackSocket(67, 66); }

inline std::array<int, 2> MakePipe() noexcept {
  auto sock = MakeListenPart();
  auto write_sock = MakeWritePart();
  return {sock, write_sock};
}

inline void WritePipe(std::array<int, 2> const& pipe) noexcept {
  if (pipe[1] != -1) {
    send(pipe[1], "1", 1, 0);
  }
}

inline void ReadPipe(std::array<int, 2> const& pipe) noexcept {
  if (pipe[0] != -1) {
    std::uint8_t buff[16];
    auto n = recv(pipe[0], &buff, sizeof(buff), 0);
    (void)(n);
  }
}

inline void ClosePipe(std::array<int, 2>& pipe) noexcept {
  if (pipe[1] != -1) {
    close(pipe[1]);
    pipe[1] = -1;
  }
  if (pipe[0] != -1) {
    close(pipe[0]);
    pipe[0] = -1;
  }
}

EventType FromPollEvent(std::uint32_t events) {
  EventType event_type{0};

  if ((events & POLLIN) != 0) {
    event_type |= EventType::kRead;
  }
  if ((events & POLLOUT) != 0) {
    event_type |= EventType::kWrite;
  }
  if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    event_type |= EventType::kError;
  }
  return event_type;
}

}  // namespace freertos_poller_internal

FreeRtosLwipPollerImpl::FreeRtosLwipPollerImpl()
    : wake_up_pipe_{freertos_poller_internal::MakePipe()},
      stop_requested_{false} {
  assert(wake_up_pipe_[0] != -1);
  assert(wake_up_pipe_[1] != -1);

  my_task_handle_ = xTaskGetCurrentTaskHandle();

  auto res = xTaskCreate(
      [](void* arg) noexcept {
        auto* poller = static_cast<FreeRtosLwipPollerImpl*>(arg);
        poller->Loop();
        // notify - the task is finished
        xTaskNotifyGive(poller->my_task_handle_);
        // delete the task
        vTaskDelete(nullptr);
      },
      "FreeRTOS lwip poller loop", kStackSize, static_cast<void*>(this),
      tskIDLE_PRIORITY + 1, &worker_task_handle_);
  if (res != pdPASS) {
    AE_TELED_ERROR("Failed to create poll worker task: {}", res);
  } else {
    AE_TELE_DEBUG(kFreertosWorkerCreate, "Poll worker was created");
  }
}

FreeRtosLwipPollerImpl::~FreeRtosLwipPollerImpl() {
  stop_requested_.store(true, std::memory_order::release);
  freertos_poller_internal::WritePipe(wake_up_pipe_);
  // if task was created
  if (worker_task_handle_ != nullptr) {
    // wait for the task to stop (join)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  freertos_poller_internal::ClosePipe(wake_up_pipe_);

  AE_TELE_DEBUG(kFreertosWorkerDestroyed, "Poll worker has been destroyed");
}

void FreeRtosLwipPollerImpl::lock() { ctl_mutex_.lock(); }

void FreeRtosLwipPollerImpl::unlock() { ctl_mutex_.unlock(); }

void FreeRtosLwipPollerImpl::Event(DescriptorType fd, EventType event_type,
                                   EventCb cb) {
  AE_TELE_DEBUG(kFreertosAddDescriptor, "Added descriptor {} event {}", fd,
                event_type);
  event_map_.insert_or_assign(fd, PollEvent{event_type, std::move(cb)});
  // wake up the poller task to update poll
  freertos_poller_internal::WritePipe(wake_up_pipe_);
}

void FreeRtosLwipPollerImpl::Remove(DescriptorType fd) {
  event_map_.erase(fd);
  freertos_poller_internal::WritePipe(wake_up_pipe_);
  AE_TELE_DEBUG(kFreertosRemoveDescriptor, "Removed descriptor {}", fd);
}

void FreeRtosLwipPollerImpl::Loop() {
  static constexpr int kPollingTimeout = 16000;
  std::vector<pollfd> fds_vector;
  while (!stop_requested_.load(std::memory_order::acquire)) {
    {
      auto lock = std::scoped_lock{ctl_mutex_};
      FillFdsVector(fds_vector);
    }
    auto res = lwip_poll(fds_vector.data(), fds_vector.size(), kPollingTimeout);
    if (res == -1) {
      AE_TELE_ERROR(kFreertosWaitFailed, "Polling error {} {}", errno,
                    strerror(errno));
      continue;
    }
    if (res == 0) {
      // timeout
      continue;
    }

    auto lock = std::scoped_lock{ctl_mutex_};

    for (auto const& v : fds_vector) {
      if (v.revents == 0) {
        continue;
      }
      if ((v.fd == wake_up_pipe_[0])) {
        if ((v.revents & POLLIN) != 0) {
          freertos_poller_internal::ReadPipe(wake_up_pipe_);
        }
        continue;
      }

      auto poll_event = event_map_.find(v.fd);
      if (poll_event == event_map_.end()) {
        continue;
      }
      poll_event->second.cb(v.fd,
                            freertos_poller_internal::FromPollEvent(v.revents));
    }
  }
}

void FreeRtosLwipPollerImpl::FillFdsVector(std::vector<pollfd>& fds) {
  fds.clear();
  // all events and wake up pipe
  fds.reserve(event_map_.size() + 1);
  {
    pollfd pfd;
    pfd.fd = wake_up_pipe_[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    fds.push_back(pfd);
  }

  for (const auto& desc : event_map_) {
    pollfd pfd;
    pfd.fd = desc.first;
    pfd.events = std::invoke(
        [](EventType event_type) {
          short events = 0;
          for (auto e :
               {EventType::kRead, EventType::kWrite, EventType::kError}) {
            if ((event_type & e) == 0) {
              continue;
            }
            switch (e) {
              case EventType::kRead:
                events |= POLLIN;
                break;
              case EventType::kWrite:
                events |= POLLOUT;
                break;
              case EventType::kError:
                events |= POLLERR | POLLHUP | POLLNVAL;
                break;
              default:
                break;
            }
          }
          return events;
        },
        desc.second.event_type);
    pfd.revents = 0;
    fds.push_back(pfd);
  }
}

FreeRtosPolledFd::FreeRtosPolledFd(DescriptorType fd,
                                   std::shared_ptr<NativePoller> const& poller)
    : fd_{fd},
      poller_{std::static_pointer_cast<FreeRtosLwipPollerImpl>(poller)} {}

FreeRtosPolledFd::~FreeRtosPolledFd() {
  if (fd_ != kInvalidDescriptor) {
    auto lock = std::scoped_lock(*poller_);
    poller_->Remove(fd_);
  }
}

void FreeRtosPolledFd::Event(EventType event_type,
                             FreeRtosLwipPollerImpl::EventCb event_cb) {
  auto lock = std::scoped_lock(*poller_);
  poller_->Event(fd_, event_type, std::move(event_cb));
}

FreeRtosPolledFd::Fd FreeRtosPolledFd::fd() const noexcept {
  return Fd{std::unique_lock{*poller_}, fd_};
}

FreeRtosPolledFd::Fd FreeRtosPolledFd::Remove() noexcept {
  auto fd = Fd{std::unique_lock{*poller_}, fd_};
  if (fd_ != kInvalidDescriptor) {
    poller_->Remove(fd_);
    fd_ = kInvalidDescriptor;
  }
  return fd;
}

FreertosPoller::FreertosPoller() = default;

FreertosPoller::FreertosPoller(ObjProp prop) : IPoller{prop} {}

std::shared_ptr<NativePoller> FreertosPoller::Native() {
  if (!impl_) {
    impl_ = std::make_shared<FreeRtosLwipPollerImpl>();
  }
  return impl_;
}

}  // namespace ae

#endif

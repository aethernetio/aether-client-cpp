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
  if (bind(l_socket, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) !=
      0) {
    AE_TELED_ERROR("Bind listen socket failed with {}", errno);
    close(l_socket);
    assert(false);
    return -1;
  }
  if (connect(l_socket, reinterpret_cast<sockaddr const*>(&addr),
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

inline void WritePipe(std::array<int, 2> const& pipe) {
  if (pipe[1] != -1) {
    send(pipe[1], "1", 1, 0);
  }
}

inline void ReadPipe(std::array<int, 2> const& pipe) {
  if (pipe[0] != -1) {
    std::uint8_t buff[16];
    auto n = recv(pipe[0], &buff, sizeof(buff), 0);
    (void)(n);
  }
}

inline void ClosePipe(std::array<int, 2>& pipe) {
  if (pipe[1] != -1) {
    close(pipe[1]);
    pipe[1] = -1;
  }
  if (pipe[0] != -1) {
    close(pipe[0]);
    pipe[0] = -1;
  }
}

EventType FromEpollEvent(std::uint32_t events) {
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

void vTaskFunction(void* pvParameters) {
  auto* poller = static_cast<FreeRtosLwipPollerImpl*>(pvParameters);
  poller->Loop();
}

FreeRtosLwipPollerImpl::FreeRtosLwipPollerImpl()
    : wake_up_pipe_{freertos_poller_internal::MakePipe()},
      stop_requested_{false} {
  assert(wake_up_pipe_[0] != -1);
  assert(wake_up_pipe_[1] != -1);

  xTaskCreate(static_cast<void (*)(void*)>(&vTaskFunction), "Poller loop", 4096,
              static_cast<void*>(this), tskIDLE_PRIORITY, &myTaskHandle_);
  AE_TELE_DEBUG(kFreertosWorkerCreate, "Poll worker was created");
}

FreeRtosLwipPollerImpl::~FreeRtosLwipPollerImpl() {
  stop_requested_ = true;
  freertos_poller_internal::WritePipe(wake_up_pipe_);
  if (myTaskHandle_ != nullptr) {
    vTaskDelete(myTaskHandle_);
  }
  freertos_poller_internal::ClosePipe(wake_up_pipe_);
  AE_TELE_DEBUG(kFreertosWorkerDestroyed, "Poll worker has been destroyed");
}

void FreeRtosLwipPollerImpl::Event(DescriptorType fd, EventType event_type,
                                   EventCb cb) {
  auto lock = std::scoped_lock{ctl_mutex_};
  AE_TELE_DEBUG(kFreertosAddDescriptor, "Added descriptor {} event {}", fd,
                event_type);
  event_map_.insert_or_assign(fd, PollEvent{event_type, std::move(cb)});
  // wake up the poller task to update poll
  freertos_poller_internal::WritePipe(wake_up_pipe_);
}

void FreeRtosLwipPollerImpl::Remove(DescriptorType fd) {
  auto lock = std::scoped_lock{ctl_mutex_};
  event_map_.erase(fd);
  freertos_poller_internal::WritePipe(wake_up_pipe_);
  AE_TELE_DEBUG(kFreertosRemoveDescriptor, "Removed descriptor {}", fd);
}

void FreeRtosLwipPollerImpl::Loop() {
  static constexpr int kPollingTimeout = 16000;
  while (!stop_requested_) {
    std::vector<pollfd> fds_vector;
    {
      auto lock = std::scoped_lock{ctl_mutex_};
      fds_vector = FillFdsVector();
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
      poll_event->second.cb(
          freertos_poller_internal::FromEpollEvent(v.revents));
    }
  }
}

std::vector<pollfd> FreeRtosLwipPollerImpl::FillFdsVector() {
  std::vector<pollfd> fds;
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

  return fds;
}

FreertosPoller::FreertosPoller() = default;

FreertosPoller::FreertosPoller(ObjProp prop) : IPoller{prop} {}

NativePoller* FreertosPoller::Native() {
  if (!impl_) {
    impl_.emplace();
  }
  return static_cast<NativePoller*>(&*impl_);
}

}  // namespace ae

#endif

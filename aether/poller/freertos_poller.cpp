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

#  include "aether/tele/tele.h"

namespace ae {
void vTaskFunction(void *pvParameters);

class FreertosPoller::PollWorker {
 public:
  PollWorker() {
    xTaskCreate(static_cast<void (*)(void *)>(&vTaskFunction), "Poller loop",
                8192, static_cast<void *>(this), tskIDLE_PRIORITY,
                &myTaskHandle);
    AE_TELED_DEBUG("Poll worker was created");
  }

  ~PollWorker() {
    vTaskResume(myTaskHandle);
    if (myTaskHandle != NULL) {
      vTaskDelete(myTaskHandle);
    }
    AE_TELED_DEBUG("Poll worker has been destroed");
  }

  [[nodiscard]] OnPollEventSubscriber Add(DescriptorType descriptor) {
    auto lock = std::unique_lock(ctl_mutex_);
    AE_TELED_DEBUG("Added descriptor {}", descriptor);
    descriptors_.insert(descriptor);
    vTaskResume(myTaskHandle);
    return OnPollEventSubscriber{poll_event_, std::move(lock)};
  }

  void Remove(DescriptorType descriptor) {
    auto lock = std::lock_guard(ctl_mutex_);
    descriptors_.erase(descriptor);
    AE_TELED_DEBUG("Removed descriptor {}", descriptor);
    vTaskResume(myTaskHandle);
  }

  void Loop(void) {
    int res;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = kTaskDelay;
    const int timeout = kPollingTimeout;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
      {
        auto lock = std::lock_guard{ctl_mutex_};
        fds_vector = FillFdsVector(descriptors_);
      }
      if (!fds_vector.empty()) {
        res = lwip_poll(fds_vector.data(), fds_vector.size(), timeout);
        if (res == -1) {
          AE_TELED_ERROR("Socket polling has an error {} {}", errno,
                         strerror(errno));
          continue;
        }

        {
          auto lock = std::lock_guard{ctl_mutex_};
          for (auto &v : fds_vector) {
            for (auto event : FromEpollEvent(v.revents)) {
              poll_event_.Emit(PollerEvent{v.fd, event});
            }
          }
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
      } else {
        vTaskSuspend(myTaskHandle);
      }
    }
  }

 private:
  TaskHandle_t myTaskHandle = nullptr;

  std::vector<pollfd> FillFdsVector(std::set<int> const &descriptors) {
    std::vector<pollfd> fds_vector_l;
    fds_vector_l.reserve(descriptors_.size());
    pollfd fds;

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
    res.resize(3);
    if ((events & POLLIN) != 0) {
      res.push_back(EventType::kRead);
    }
    if ((events & POLLOUT) != 0) {
      res.push_back(EventType::kWrite);
    }
    return res;
  }

  OnPollEvent poll_event_;
  std::set<int> descriptors_;
  std::vector<pollfd> fds_vector;
  std::recursive_mutex ctl_mutex_;
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
  assert(poll_worker_);
  poll_worker_->Remove(descriptor);
}

void FreertosPoller::InitPollWorker() {
  poll_worker_ = std::make_unique<PollWorker>();
}
}  // namespace ae

#endif

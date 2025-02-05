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

#  include <map>
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
  std::map<PollerEvent, Callback> callbacks_;
  std::vector<pollfd> fds_vector;
  std::mutex ctl_mutex_;

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

  void Add(PollerEvent event, Callback callback) {
    auto lock = std::lock_guard(ctl_mutex_);
    callbacks_.emplace(event, std::move(callback));
    AE_TELED_DEBUG("Added event {}", event);
    vTaskResume(myTaskHandle);
  }

  void Remove(PollerEvent event) {
    auto lock = std::lock_guard(ctl_mutex_);
    auto it = callbacks_.find(event);
    if (it != callbacks_.end()) {
      callbacks_.erase(it);
      AE_TELED_DEBUG("Removed event {}", event);
    }
    vTaskResume(myTaskHandle);
  }

  void Loop(void) {
    int r;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = TASK_DELAY;
    const int timeout = POLLING_TIMEOUT;

    xLastWakeTime = xTaskGetTickCount();

    while (1) {
      {
        auto lock = std::lock_guard{ctl_mutex_};
        fds_vector = FillFdsVector(callbacks_);
      }
      if (!fds_vector.empty()) {
        r = lwip_poll(fds_vector.data(), fds_vector.size(), timeout);
        if (r == -1) {
          AE_TELED_ERROR("Socket polling has an error {} {}", errno,
                         strerror(errno));
          continue;
        }

        {
          auto lock = std::lock_guard{ctl_mutex_};
          for (auto &v : fds_vector) {
            PollerEvent event{v.fd, FromEpollEvent(v.revents)};

            auto it = callbacks_.find(event);
            if (it == callbacks_.end()) {
              AE_TELED_ERROR("Failed to find callback for PollerEvent {}",
                             event);
              continue;
            }

            auto &cb = it->second;
            cb(event);
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

  std::vector<pollfd> FillFdsVector(
      std::map<PollerEvent, Callback> call_backs) {
    std::vector<pollfd> fds_vector_l;
    pollfd fds;

    for (const auto &entry : call_backs) {
      fds.fd = entry.first.descriptor;
      switch (entry.first.event_type) {
        case EventType::READ:
          fds.events = POLLIN;
          break;
        case EventType::WRITE:
          fds.events = POLLOUT;
          break;
        case EventType::ANY:
          fds.events = POLLIN | POLLOUT;
          break;
      }
      fds.revents = 0;
      fds_vector_l.push_back(fds);
    }

    return fds_vector_l;
  }

  EventType FromEpollEvent(uint32_t events) {
    if (events == POLLIN) {
      return EventType::READ;
    }
    if (events == POLLOUT) {
      return EventType::WRITE;
    }
    return EventType::ANY;
  }
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

void FreertosPoller::Add(PollerEvent event, Callback callback) {
  if (!poll_worker_) {
    InitPollWorker();
  }
  poll_worker_->Add(event, std::move(callback));
}

void FreertosPoller::Remove(PollerEvent event) {
  assert(poll_worker_);
  poll_worker_->Remove(event);
}

void FreertosPoller::InitPollWorker() {
  poll_worker_ = std::make_unique<PollWorker>();
}

}  // namespace ae

#endif

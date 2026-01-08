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

#ifndef AETHER_POLLER_FREERTOS_POLLER_H_
#define AETHER_POLLER_FREERTOS_POLLER_H_

#if (defined(ESP_PLATFORM))
#  define FREERTOS_POLLER_ENABLED 1

#  include <freertos/FreeRTOS.h>  // IWYU pragma: keep
#  include <freertos/task.h>
#  include <sys/poll.h>

#  include <map>
#  include <mutex>
#  include <atomic>
#  include <optional>

#  include "aether/poller/poller.h"
#  include "aether/poller/poller_types.h"
#  include "aether/types/small_function.h"

namespace ae {
class FreeRtosLwipPollerImpl : public NativePoller {
  friend void vTaskFunction(void* pvParameters);

 public:
  using EventCb = SmallFunction<void(EventType event)>;

  struct PollEvent {
    EventType event_type;
    EventCb cb;
  };

  FreeRtosLwipPollerImpl();
  ~FreeRtosLwipPollerImpl();

  void Event(DescriptorType fd, EventType event_type, EventCb cb);
  void Remove(DescriptorType fd);

 private:
  void Loop();
  std::vector<pollfd> FillFdsVector();

  TaskHandle_t myTaskHandle_ = nullptr;
  std::array<int, 2> wake_up_pipe_;
  std::atomic_bool stop_requested_;
  std::map<DescriptorType, PollEvent> event_map_;
  std::recursive_mutex ctl_mutex_;
};

class FreertosPoller : public IPoller {
  AE_OBJECT(FreertosPoller, IPoller, 0)

  FreertosPoller();

 public:
  explicit FreertosPoller(Domain* domain);

  AE_OBJECT_REFLECT()

  NativePoller* Native() override;

 private:
  std::optional<FreeRtosLwipPollerImpl> impl_;
};

}  // namespace ae

#endif
#endif  // AETHER_POLLER_FREERTOS_POLLER_H_ */

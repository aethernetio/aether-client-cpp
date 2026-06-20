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
#  include <memory>

#  include "aether/config.h"
#  include "aether/poller/poller.h"
#  include "aether/poller/poller_types.h"
#  include "aether-miscpp/types/small_function.h"

namespace ae {
class FreeRtosLwipPollerImpl : public NativePoller {
  static constexpr std::size_t kStackSize = AE_FREERTOS_POLLER_STACK_SIZE;

 public:
  using EventCb = SmallFunction<void(DescriptorType fd, EventType event)>;

  struct PollEvent {
    EventType event_type;
    EventCb cb;
  };

  FreeRtosLwipPollerImpl();
  ~FreeRtosLwipPollerImpl() override;

  // implementation for BasicLockable
  void lock();
  void unlock();

  void Event(DescriptorType fd, EventType event_type, EventCb event_cb);
  void Remove(DescriptorType fd);

 private:
  void Loop();
  void FillFdsVector(std::vector<pollfd>& fds);

  TaskHandle_t worker_task_handle_ = nullptr;
  TaskHandle_t my_task_handle_ = nullptr;
  std::array<int, 2> wake_up_pipe_;
  std::atomic_bool stop_requested_;
  std::map<DescriptorType, PollEvent> event_map_;
  std::recursive_mutex ctl_mutex_;
};

class FreeRtosPolledFd {
 public:
  class Fd {
   public:
    Fd(std::unique_lock<FreeRtosLwipPollerImpl>&& lock,
       DescriptorType fd) noexcept
        : lock_{std::move(lock)}, fd_{fd} {}

    DescriptorType operator*() const noexcept { return fd_; }

   private:
    std::unique_lock<FreeRtosLwipPollerImpl> lock_;
    DescriptorType fd_;
  };

  FreeRtosPolledFd(DescriptorType fd,
                   std::shared_ptr<NativePoller> const& poller);

  ~FreeRtosPolledFd();

  void Event(EventType event_type, FreeRtosLwipPollerImpl::EventCb event_cb);
  Fd fd() const noexcept;
  Fd Remove() noexcept;

 private:
  DescriptorType fd_;
  mutable std::shared_ptr<FreeRtosLwipPollerImpl> poller_;
};

class FreertosPoller : public IPoller {
  AE_OBJECT(FreertosPoller, IPoller, 0)

  FreertosPoller();

 public:
  explicit FreertosPoller(ObjProp prop);

  AE_OBJECT_REFLECT()

  std::shared_ptr<NativePoller> Native() override;

 private:
  std::shared_ptr<FreeRtosLwipPollerImpl> impl_;
};

}  // namespace ae

#endif
#endif  // AETHER_POLLER_FREERTOS_POLLER_H_

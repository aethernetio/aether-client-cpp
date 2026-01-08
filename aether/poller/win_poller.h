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

#ifndef AETHER_POLLER_WIN_POLLER_H_
#define AETHER_POLLER_WIN_POLLER_H_

#if defined _WIN32
#  define WIN_POLLER_ENABLED 1

#  include <windows.h>  // for OVERLAPPED and iocp

// remove this buggy windows macros
#  if defined RegisterClass
#    undef RegisterClass
#  endif

#  include <map>
#  include <mutex>
#  include <thread>
#  include <atomic>
#  include <optional>

#  include "aether/poller/poller.h"
#  include "aether/poller/poller_types.h"
#  include "aether/types/small_function.h"

namespace ae {
class IoCpPoller final : public NativePoller {
 public:
  using EventCb = SmallFunction<void(LPOVERLAPPED overlapped)>;

  IoCpPoller();
  ~IoCpPoller();

  void Add(DescriptorType descriptor, EventCb cb);
  void Remove(DescriptorType descriptor);

 private:
  void Loop();

  HANDLE iocp_ = INVALID_HANDLE_VALUE;

  std::mutex poller_mutex_;
  std::map<DescriptorType, EventCb> event_map_;
  std::atomic_bool stop_requested_{false};
  std::thread loop_thread_;
};

class WinPoller : public IPoller {
  AE_OBJECT(WinPoller, IPoller, 0)

  class IoCPPoller;

  WinPoller();

 public:
  explicit WinPoller(Domain* domain);
  ~WinPoller() override;

  AE_OBJECT_REFLECT()

  NativePoller* Native() override;

 private:
  std::optional<IoCpPoller> impl_;
};
}  // namespace ae
#endif
#endif  // AETHER_POLLER_WIN_POLLER_H_

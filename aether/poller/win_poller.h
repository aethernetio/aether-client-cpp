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

#  include <memory>

#  include "aether/poller/poller.h"

namespace ae {

// structure to mark async operations
struct WinPollerOverlapped {
  OVERLAPPED overlapped;
  EventType event_type;
};

class WinPoller : public IPoller {
  AE_OBJECT(WinPoller, IPoller, 0)

 private:
  class IoCPPoller;

 public:
#  if defined AE_DISTILLATION
  WinPoller(Domain* domain);
#  endif

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }

  void Add(PollerEvent event, Callback callback) override;
  void Remove(PollerEvent event) override;

 private:
  std::shared_ptr<IoCPPoller> iocp_poller_;
};
}  // namespace ae
#endif
#endif  // AETHER_POLLER_WIN_POLLER_H_
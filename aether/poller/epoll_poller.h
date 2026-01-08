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

#ifndef AETHER_POLLER_EPOLL_POLLER_H_
#define AETHER_POLLER_EPOLL_POLLER_H_

#if defined __linux__
#  define EPOLL_POLLER_ENABLED 1

#  include <mutex>
#  include <thread>
#  include <atomic>
#  include <optional>

#  include "aether/poller/poller.h"
#  include "aether/poller/unix_poller.h"

namespace ae {
class EpollImpl final : public UnixPollerImpl {
 public:
  EpollImpl();
  ~EpollImpl() override;

  void Event(DescriptorType fd, EventType event, EventCb cb) override;
  void Remove(DescriptorType fd) override;

 private:
  static int InitEpoll();
  static int MakeEventFd();
  void EmptyWakeUpPipe(EventType event);
  void Loop();

  int epoll_fd_;
  int event_fd_;
  std::recursive_mutex poller_mutex_;
  std::map<DescriptorType, EventCb> event_map_;

  std::atomic_bool stop_requested_{false};
  std::thread thread_;
};

class EpollPoller : public IPoller {
  AE_OBJECT(EpollPoller, IPoller, 0)

  EpollPoller();

 public:
  explicit EpollPoller(Domain* domain);

  AE_OBJECT_REFLECT()

  NativePoller* Native() override;

 private:
  std::optional<EpollImpl> impl_;
};

}  // namespace ae

#endif
#endif  // AETHER_POLLER_EPOLL_POLLER_H_ */

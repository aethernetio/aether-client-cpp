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

#ifndef AETHER_POLLER_KQUEUE_POLLER_H_
#define AETHER_POLLER_KQUEUE_POLLER_H_

#if defined __APPLE__ || defined __FreeBSD__
#  define KQUEUE_POLLER_ENABLED 1

#  include <mutex>
#  include <atomic>
#  include <thread>
#  include <optional>

#  include "aether/poller/poller.h"
#  include "aether/poller/unix_poller.h"

namespace ae {
class KqueuePollerImpl : public UnixPollerImpl {
 public:
  KqueuePollerImpl();
  ~KqueuePollerImpl() override;

  void Event(DescriptorType fd, EventType event, EventCb cb) override;
  void Remove(DescriptorType descriptor) override;

 private:
  static int InitKqueue();
  void Loop();

  const int exit_kqueue_event_ = 1;

  int kqueue_fd_;

  std::recursive_mutex poller_mutex_;
  std::map<DescriptorType, EventCb> event_map_;
  std::atomic_bool stop_requested_{false};
  std::thread thread_;
};

class KqueuePoller : public IPoller {
  AE_OBJECT(KqueuePoller, IPoller, 0)

  KqueuePoller();

 public:
  KqueuePoller(Domain* domain);

  AE_OBJECT_REFLECT()

  NativePoller* Native() override;

 private:
  std::optional<KqueuePollerImpl> impl_;
};
}  // namespace ae

#endif
#endif  // AETHER_POLLER_KQUEUE_POLLER_H_ */

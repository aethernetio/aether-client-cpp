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

#  include <memory>

#  include "aether/poller/poller.h"

namespace ae {
class KqueuePoller : public IPoller {
  AE_OBJECT(KqueuePoller, IPoller, 0)

  class PollerWorker;

  KqueuePoller();

 public:
#  if defined AE_DISTILLATION
  KqueuePoller(Domain* domain);
#  endif
  ~KqueuePoller() override;

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }

  void Add(PollerEvent event, Callback callback) override;
  void Remove(PollerEvent event) override;

 private:
  void InitPollWorker();

  std::unique_ptr<PollerWorker> poller_worker_;
};
}  // namespace ae

#endif
#endif  // AETHER_POLLER_KQUEUE_POLLER_H_ */

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

#  include <memory>

#  include "aether/poller/poller.h"

namespace ae {

class FreertosPoller : public IPoller {
  AE_OBJECT(FreertosPoller, IPoller, 0)

  static constexpr int POLLING_TIMEOUT = -1;
  static constexpr int TASK_DELAY = 1;

 public:
  class PollWorker;
#  if defined AE_DISTILLATION
  FreertosPoller(Domain* domain);
#  endif

  ~FreertosPoller() override;

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }

  void Add(PollerEvent event, Callback callback) override;
  void Remove(PollerEvent event) override;

 private:
  void InitPollWorker();

  std::shared_ptr<PollWorker> poll_worker_;
};

}  // namespace ae

#endif
#endif  // AETHER_POLLER_FREERTOS_POLLER_H_ */
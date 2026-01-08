/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_POLLER_UNIX_POLLER_H_
#define AETHER_POLLER_UNIX_POLLER_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__)

#  include "aether/poller/poller.h"
#  include "aether/poller/poller_types.h"
#  include "aether/types/small_function.h"

namespace ae {
/**
 * \brief Base class for unix poller implementation
 */
class UnixPollerImpl : public NativePoller {
 public:
  using EventCb = SmallFunction<void(EventType event)>;

  virtual ~UnixPollerImpl() = default;

  /**
   * \brief Add or change file descriptor to the poller.
   * fd - file descriptor to add or change.
   * event - event to set. It contains file descriptor and ORed event list to
   * wait for.
   * cb - callback to call when event occurs.
   */
  virtual void Event(DescriptorType fd, EventType event, EventCb cb) = 0;
  /**
   * \brief Remove file descriptor from the poller.
   */
  virtual void Remove(DescriptorType descriptor) = 0;
};
}  // namespace ae

#endif
#endif  // AETHER_POLLER_UNIX_POLLER_H_

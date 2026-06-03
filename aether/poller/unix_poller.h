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

#  include <mutex>
#  include <utility>

#  include "aether/poller/poller.h"
#  include "aether/poller/poller_types.h"
#  include "aether/types/small_function.h"

namespace ae {
/**
 * \brief Base class for unix poller implementation
 */
class UnixPollerImpl : public NativePoller {
 public:
  using EventCb = SmallFunction<void(DescriptorType fd, EventType event)>;

 private:
  friend class UnixPolledFd;
  friend class std::scoped_lock<UnixPollerImpl>;
  friend class std::unique_lock<UnixPollerImpl>;

  virtual void lock() = 0;
  virtual void unlock() = 0;

  /**
   * \brief Add file descriptor to the poller with event callback.
   * fd - file descriptor to add.
   * cb - callback to call when event occurs.
   */
  virtual void Callback(DescriptorType fd, EventCb cb) = 0;
  /**
   * \brief Setup event poller for file descriptor
   */
  virtual void Event(DescriptorType fd, EventType events) = 0;
  /**
   * \brief Remove file descriptor from the poller.
   */
  virtual void Remove(DescriptorType descriptor) = 0;
};

class UnixPolledFd {
 public:
  class Fd {
   public:
    Fd(std::unique_lock<UnixPollerImpl>&& lock, DescriptorType fd) noexcept
        : lock_{std::move(lock)}, fd_{fd} {}

    DescriptorType operator*() const noexcept { return fd_; }

   private:
    std::unique_lock<UnixPollerImpl> lock_;
    DescriptorType fd_;
  };

  UnixPolledFd(DescriptorType fd, std::shared_ptr<NativePoller> const& poller,
               UnixPollerImpl::EventCb cb)
      : fd_{fd}, poller_{std::static_pointer_cast<UnixPollerImpl>(poller)} {
    auto lock = std::scoped_lock{*poller_};
    poller_->Callback(fd_, std::move(cb));
  }

  ~UnixPolledFd() {
    auto lock = std::scoped_lock{*poller_};
    if (fd_ != kInvalidDescriptor) {
      poller_->Remove(fd_);
    }
  }

  void Events(EventType events) {
    auto lock = std::scoped_lock{*poller_};
    poller_->Event(fd_, events);
  }

  auto fd() const noexcept { return Fd{std::unique_lock{*poller_}, fd_}; }

  /**
   * \brief Use Remove to remove fd from the poller and return the descriptor
   */
  auto Remove() noexcept {
    auto fd = Fd{std::unique_lock{*poller_}, fd_};
    if (fd_ != kInvalidDescriptor) {
      poller_->Remove(fd_);
      fd_ = kInvalidDescriptor;
    }
    return fd;
  }

 private:
  DescriptorType fd_;
  mutable std::shared_ptr<UnixPollerImpl> poller_;
};

}  // namespace ae

#endif
#endif  // AETHER_POLLER_UNIX_POLLER_H_

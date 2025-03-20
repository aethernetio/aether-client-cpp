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

#ifndef AETHER_ACTIONS_ACTION_H_
#define AETHER_ACTIONS_ACTION_H_

#include <utility>

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/actions/action_trigger.h"
#include "aether/actions/action_registry.h"

namespace ae {

// Base Action class
class IAction {
 public:
  virtual ~IAction() = default;

  virtual TimePoint Update(TimePoint current_time) = 0;
};

/**
 * \brief Common action done/reject interface.
 * Inherit from this class to implement your own action.
 * Call Result() in case of success, Error in case of failure and Stop() in case
 * of rejection.
 */
template <typename T>
class Action : public IAction {
 public:
  using Base = Action<T>;

  Action() = default;

  template <typename TActionContext,
            typename _ = std::void_t<
                decltype(std::declval<TActionContext>().get_registry()),
                decltype(std::declval<TActionContext>().get_trigger())>>
  explicit Action(TActionContext&& action_context)
      : action_trigger_{&action_context.get_trigger()},
        index_{action_context.get_registry().Register(*this)} {
    Trigger();
  }

  Action(Action const& other) = delete;
  Action(Action&& other) noexcept
      : action_trigger_{other.action_trigger_},
        index_{std::move(other.index_)} {
    // replace the action
    if (auto* it = index_.iterator(); it) {
      (*it)->action = this;
    }
  }

  ~Action() override { index_.Erase(); }

  Action& operator=(Action const& other) = delete;

  Action& operator=(Action&& other) noexcept {
    if (this != &other) {
      action_trigger_ = other.action_trigger_;
      index_ = std::move(other.index_);
      // replace the action
      if (auto* it = index_.iterator(); it) {
        (*it)->action = this;
      }
    }
    return *this;
  }

  /**
   * Add callback to be called when action is done.
   */
  template <typename Func>
  [[nodiscard]] auto SubscribeOnResult(Func&& callback) {
    return EventSubscriber{result_cbs_}.Subscribe(std::forward<Func>(callback));
  }

  /**
   * Add callback to be called when action is rejected.
   */
  template <typename Func>
  [[nodiscard]] auto SubscribeOnError(Func&& callback) {
    return EventSubscriber{error_cbs_}.Subscribe(std::forward<Func>(callback));
  }

  /**
   * Add callback to be called when action is stopped.
   */
  template <typename Func>
  [[nodiscard]] auto SubscribeOnStop(Func&& callback) {
    return EventSubscriber{stop_cbs_}.Subscribe(std::forward<Func>(callback));
  }

  EventSubscriber<void()> FinishedEvent() {
    return EventSubscriber{finished_event_};
  }

  // Call finish if action finished all it's job and may be removed.
  void Finish() {
    Trigger();
    finished_event_.Emit();
  }

  // get index of action in registry
  auto index() const {
    assert(index_.get() != nullptr);
    return index_;
  }

 protected:
  // Call trigger if action has new state to update
  void Trigger() {
    if (action_trigger_ != nullptr) {
      action_trigger_->Trigger();
    }
  }

  // Call result to mark action as done and call all result callbacks
  void Result(T& object) {
    result_cbs_.Emit(object);
    Finish();
  }

  // Call result repeat to call all result callbacks without marking action as
  // finished
  void ResultRepeat(T& object) { result_cbs_.Emit(object); }

  //  Call error to mark action as failed and call all error callbacks
  void Error(T& object) {
    error_cbs_.Emit(object);
    Finish();
  }

  // Call stop to mark action as rejected and call all stop callbacks
  void Stop(T& object) {
    stop_cbs_.Emit(object);
    Finish();
  }

  Event<void(T&)> result_cbs_;
  Event<void(T&)> error_cbs_;
  Event<void(T&)> stop_cbs_;
  Event<void()> finished_event_;

  ActionTrigger* action_trigger_{};
  ActionRegistry::IndexShare index_;
};

template <typename T = void>
class NotifyAction : public Action<NotifyAction<T>> {
 public:
  using Action<NotifyAction<T>>::Action;
  using Action<NotifyAction<T>>::operator=;

  TimePoint Update(TimePoint current_time) override {
    if (notify_success_) {
      notify_success_ = false;
      this->ResultRepeat(static_cast<T&>(*this));
    }
    if (notify_failed_) {
      notify_failed_ = false;
      this->Error(static_cast<T&>(*this));
    }
    return current_time;
  }

  void Notify() {
    notify_success_ = true;
    this->Trigger();
  }
  void Failed() {
    notify_failed_ = true;
    this->Trigger();
  }

 private:
  bool notify_success_{};
  bool notify_failed_{};
};

template <>
class NotifyAction<void> : public Action<NotifyAction<void>> {
 public:
  using Action<NotifyAction<void>>::Action;
  using Action<NotifyAction<void>>::operator=;

  TimePoint Update(TimePoint current_time) override {
    if (notify_success_) {
      notify_success_ = false;
      this->ResultRepeat(*this);
    }
    if (notify_failed_) {
      notify_failed_ = false;
      this->Error(*this);
    }
    return current_time;
  }

  void Notify() {
    notify_success_ = true;
    this->Trigger();
  }
  void Failed() {
    notify_failed_ = true;
    this->Trigger();
  }

 private:
  bool notify_success_{};
  bool notify_failed_{};
};

}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_H_

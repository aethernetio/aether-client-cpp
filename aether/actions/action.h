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

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/actions/action_result.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_trigger.h"

namespace ae {
namespace action_internal {
template <typename T, typename Enabled = void>
struct HasUpdate : std::false_type {};

template <typename T>
struct HasUpdate<T, std::void_t<decltype(&T::Update)>> : std::true_type {};
}  // namespace action_internal

// Base Action class
class IAction {
 public:
  virtual ~IAction() = default;

  virtual TimePoint ActionUpdate(TimePoint current_time) = 0;
  virtual bool IsFinished() const = 0;
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
  explicit Action(ActionContext action_context)
      : action_trigger_{&action_context.get_trigger()} {
    Trigger();
  }

  AE_CLASS_MOVE_ONLY(Action);

  TimePoint ActionUpdate(TimePoint current_time) override {
    if constexpr (!action_internal::HasUpdate<T>::value) {
      return current_time;
    } else {
      auto& self = *static_cast<T*>(this);
      ActionResult res;
      if constexpr (std::is_invocable_v<decltype(&T::Update), T*, TimePoint>) {
        res = self.Update(current_time);
      } else {
        res = self.Update();
      }
      switch (res.type) {
        case ResultType::kNothing: {
          break;
        }
        case ResultType::kResult: {
          Result(self);
          break;
        }
        case ResultType::kError: {
          Error(self);
          break;
        }
        case ResultType::kStop: {
          Stop(self);
          break;
        }
        case ResultType::kDelay: {
          return res.delay_to;
        }
      }
      return current_time;
    }
  }

  /**
   * Add callback to be called when action is done.
   */
  [[nodiscard]] auto ResultEvent() { return EventSubscriber{result_cbs_}; }

  /**
   * Add callback to be called when action is rejected.
   */
  [[nodiscard]] auto ErrorEvent() { return EventSubscriber{error_cbs_}; }

  /**
   * Add callback to be called when action is stopped.
   */
  [[nodiscard]] auto StopEvent() { return EventSubscriber{stop_cbs_}; }

  [[nodiscard]] auto FinishedEvent() {
    return EventSubscriber{finished_event_};
  }

  bool IsFinished() const override { return finished_; }

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

  // Call finish if action finished all it's job and may be removed.
  void Finish() {
    Trigger();
    finished_ = true;
    finished_event_.Emit();
  }

  Event<void(T&)> result_cbs_;
  Event<void(T&)> error_cbs_;
  Event<void(T&)> stop_cbs_;
  Event<void()> finished_event_;

  ActionTrigger* action_trigger_{};
  bool finished_{false};
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_H_

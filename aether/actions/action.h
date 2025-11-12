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

// IWYU pragma: begin_exports
#include "aether/events/events.h"
#include "aether/actions/update_status.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_trigger.h"
#include "aether/actions/action_event_status.h"
// IWYU pragma: end_exports

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
  explicit IAction(ActionTrigger* action_trigger)
      : action_trigger_{action_trigger} {}

  virtual ~IAction() = default;

  AE_CLASS_MOVE_ONLY(IAction);

  virtual TimePoint ActionUpdate(TimePoint current_time) = 0;

  bool IsFinished() const { return finished_; }

  [[nodiscard]] auto FinishedEvent() {
    return EventSubscriber{finished_event_};
  }

  // Trigger an event on action which leads to run action's Update as soon as
  // possible.
  void Trigger() {
    if (action_trigger_ != nullptr) {
      action_trigger_->Trigger();
    }
  }

  // Action is finished all it's job and may be removed.
  void Finish() {
    Trigger();
    finished_ = true;
    finished_event_.Emit();
  }

 protected:
  Event<void()> finished_event_;

  ActionTrigger* action_trigger_{};
  bool finished_{false};
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
      : IAction{&action_context.get_trigger()} {
    Trigger();
  }

  AE_CLASS_MOVE_ONLY(Action);

  /**
   * \brief Action Update logic.
   * Each T should provide an UpdateStatus Update([time_point]) method.
   * optional time point will be set to current_time.
   * UpdateStatus represent current Update result and may be one of:
   * kNothing, kResult, kError, kStop, kDelay
   * kNothing - indicates action currently has status update
   * kDelay - indicates action want to cal next Update at least before returned
   * time point. kResult, kError, kStop - for result, error and stop event and
   * leads to call Status method and StatusEvent.
   */
  TimePoint ActionUpdate(TimePoint current_time) override {
    if constexpr (!action_internal::HasUpdate<T>::value) {
      return current_time;
    } else {
      auto& self = *static_cast<T*>(this);
      UpdateStatus res;
      if constexpr (std::is_invocable_v<decltype(&T::Update), T*, TimePoint>) {
        res = self.Update(current_time);
      } else {
        res = self.Update();
      }
      switch (res.type) {
        case UpdateStatusType::kNothing: {
          break;
        }
        case UpdateStatusType::kResult:
        case UpdateStatusType::kError:
        case UpdateStatusType::kStop: {
          Status(self, res);
          break;
        }
        case UpdateStatusType::kDelay: {
          return res.delay_to;
        }
      }
      return current_time;
    }
  }

  /**
   * Add callback to be called when action is done result is encoded as
   * ActionEventResult.
   */
  [[nodiscard]] auto StatusEvent() { return EventSubscriber{action_result_}; }

  using IAction::Trigger;

 protected:
  // Set action status and emit status event, and make action finished.
  void Status(T& object, UpdateStatus const& result) {
    action_result_.Emit(ActionEventStatus{object, result});
    Finish();
  }

 private:
  Event<void(ActionEventStatus<T>)> action_result_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_H_

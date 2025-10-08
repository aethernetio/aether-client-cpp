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

#ifndef AETHER_ACTIONS_PIPELINE_QUEUE_H_
#define AETHER_ACTIONS_PIPELINE_QUEUE_H_

#include <queue>
#include <utility>
#include <functional>

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"

namespace ae {
template <typename TAction>
class PipelineQueue final : public Action<PipelineQueue<TAction>> {
  class StageFactory {
   public:
    using Factory = std::function<ActionPtr<TAction>()>;
    template <typename TStageRunner>
    explicit StageFactory(TStageRunner runner)
        : factory_([runner{std::move(runner)}]() mutable {
            runner.Run();
            return runner.action();
          }) {}

    ActionPtr<TAction> CreateStage() { return factory_(); }

   private:
    Factory factory_;
  };

 public:
  using BaseAction = Action<PipelineQueue<TAction>>;
  using BaseAction::BaseAction;

  UpdateStatus Update() {
    if (error_) {
      return UpdateStatus::Error();
    }
    if (stop_) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  template <typename TStageRunner>
  void Push(TStageRunner&& stage_runner) {
    queue_.emplace(std::forward<TStageRunner>(stage_runner));
    if (!running_action_) {
      RunStage();
    }
  }

  void Stop() {
    if (!running_action_) {
      stop_ = true;
      BaseAction::Trigger();
      return;
    }
    if constexpr (ActionStoppable<TAction>::value) {
      running_action_->Stop();
    } else {
      stop_ = true;
      BaseAction::Trigger();
    }
  }

 private:
  void RunStage() {
    auto& stage = queue_.front();
    running_action_ = stage.CreateStage();
    if (!running_action_) {
      error_ = true;
      BaseAction::Trigger();
      return;
    }
    stage_sub_ = running_action_->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() { NextStage(); }},
        OnError{[this]() {
          error_ = true;
          BaseAction::Trigger();
        }},
        OnStop{[this]() {
          stop_ = true;
          BaseAction::Trigger();
        }},
    });
    queue_.pop();
  }

  void NextStage() {
    if (!queue_.empty()) {
      RunStage();
    }
  }

  std::queue<StageFactory> queue_;
  ActionPtr<TAction> running_action_;
  Subscription stage_sub_;
  bool error_{};
  bool stop_{};
};
}  // namespace ae

#endif  // AETHER_ACTIONS_PIPELINE_QUEUE_H_

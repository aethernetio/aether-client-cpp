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

#ifndef AETHER_ACTIONS_ACTIONS_QUEUE_H_
#define AETHER_ACTIONS_ACTIONS_QUEUE_H_

#include <queue>
#include <utility>
#include <functional>

#include "aether/actions/action.h"
#include "aether/actions/pipeline.h"  // IWYU pragma: export
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"

namespace ae {
class ActionsQueue final : public Action<ActionsQueue> {
  class StageFactory {
   public:
    struct VTable {
      void (*stop)(void* obj);  // stop the action
      Subscription (*finished_event)(
          void* obj,
          std::function<void()>
              on_finished);  // subscribe to the action finished
    };

    using Factory = std::function<std::pair<VTable*, void*>()>;

    template <typename TStageRunner>
    explicit StageFactory(TStageRunner runner)
        : factory_([runner{std::move(runner)}]() mutable {
            runner.Run();
            auto action = runner.action();

            using ActionType = std::decay_t<decltype(*action)>;
            static VTable vtable = [&]() {
              VTable vt;
              vt.stop = [](void* obj) {
                if (obj == nullptr) {
                  return;
                }
                if constexpr (ActionStoppable<ActionType>::value) {
                  auto* a = static_cast<ActionType*>(obj);
                  a->Stop();
                }
              };
              vt.finished_event =
                  [](void* obj,
                     std::function<void()> on_finished) -> Subscription {
                if (obj == nullptr) {
                  return Subscription{};
                }
                auto* a = static_cast<ActionType*>(obj);
                return a->FinishedEvent().Subscribe(
                    [on_finished{std::move(on_finished)}]() { on_finished(); });
              };
              return vt;
            }();

            return std::make_pair(&vtable, action ? &*action : nullptr);
          }) {}

    template <typename TFunc>
    Subscription RunStage(TFunc&& on_finished) {
      std::tie(vtable_, action_obj_) = factory_();
      return vtable_->finished_event(action_obj_,
                                     std::forward<TFunc>(on_finished));
    }

    void Stop() {
      if (vtable_ == nullptr) {
        return;
      }
      vtable_->stop(action_obj_);
    }

   private:
    Factory factory_;
    VTable* vtable_{};
    void* action_obj_{};
  };

 public:
  using Action::Action;

  UpdateStatus Update() const {
    if (stop_) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  template <typename TStageRunner>
  void Push(TStageRunner&& stage_runner) {
    queue_.emplace(std::forward<TStageRunner>(stage_runner));
    if (!running_stage_) {
      RunStage();
    }
  }

  void Stop() {
    // drop the queue
    auto empty = decltype(queue_){};
    queue_.swap(empty);

    if (running_stage_) {
      running_stage_->Stop();
    }

    stop_ = true;
    Action::Trigger();
  }

 private:
  void RunStage() {
    running_stage_ = std::move(queue_.front());
    queue_.pop();
    stage_sub_ = running_stage_->RunStage([this]() { NextStage(); });

    if (!stage_sub_) {
      NextStage();
    }
  }

  void NextStage() {
    running_stage_.reset();
    if (!queue_.empty()) {
      RunStage();
    }
  }

  std::queue<StageFactory> queue_;
  std::optional<StageFactory> running_stage_;
  Subscription stage_sub_;
  bool stop_{};
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTIONS_QUEUE_H_

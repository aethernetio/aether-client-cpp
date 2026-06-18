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
#include <memory>
#include <utility>
#include <functional>

#include "aether-miscpp/types/small_function.h"
#include "aether/events/event_subscription.h"

namespace ae {
namespace actions_queue_internal {
// stage runner storage with SOO
template <typename T, std::size_t MaxSize, std::size_t MaxAlign>
class StageStorage {
  static constexpr bool kLarge =
      (sizeof(T) > MaxSize) || (alignof(T) > MaxAlign);
  using StoredType = std::conditional_t<kLarge, std::unique_ptr<T>, T>;

  static decltype(auto) MakeStored(T&& t) {
    if constexpr (kLarge) {
      return std::make_unique<T>(std::move(t));
    } else {
      return std::move(t);
    }
  }

 public:
  explicit StageStorage(T&& t) : stored_{MakeStored(std::move(t))} {}

  T& operator*() noexcept {
    if constexpr (kLarge) {
      return *stored_;
    } else {
      return stored_;
    }
  }

 private:
  StoredType stored_;
};

using FinishedHandler = SmallFunction<void(), sizeof(void*)>;

struct StageVtable {
  void (*stop)(void* obj);  // stop the action
  Subscription (*finished_event)(
      void* obj, FinishedHandler&& on_finished);  // subscribe to finished event
};

struct StageFatPtr {
  void stop() const { vtable->stop(obj); }
  decltype(auto) finished_event(FinishedHandler&& on_finished) const {
    return vtable->finished_event(obj, std::move(on_finished));
  }

  StageVtable const* vtable;
  void* obj;
};

template <typename T>
concept Finishable = requires(T& t) {
  { t.is_finished() } -> std::same_as<bool>;
  t.finished_event();
};

template <typename T>
concept Stopable = requires(T& t) { t.Stop(); };

template <typename T>
concept StageRunner = requires(T t) {
  { *std::invoke(t) } -> Finishable;
};

class StageFactory {
 public:
  // use Function for type-erasure
  using Factory = SmallFunction<StageFatPtr(), 8 * sizeof(void*)>;

  template <StageRunner T,
            typename S_ = StageStorage<std::decay_t<T>, Factory::Storage::kSize,
                                       Factory::Storage::kAlign>>
    requires(!std::is_lvalue_reference_v<T>)
  explicit StageFactory(T&& stage)  // NOLINT (*missing_std_forward)
      : factory_{[stage_ = S_{std::move(stage) /*NOLINT*/}]() mutable {
          auto* action = std::invoke(*stage_);

          using ActionType = std::decay_t<decltype(*action)>;
          static constexpr StageVtable vtable{
              .stop =
                  [](void* obj) noexcept {
                    if constexpr (Stopable<ActionType>) {
                      auto* a = static_cast<ActionType*>(obj);
                      if (a == nullptr) {
                        return;
                      }
                      a->Stop();
                    }
                  },
              .finished_event =
                  [](void* obj, FinishedHandler&& on_finished) -> Subscription {
                auto* a = static_cast<ActionType*>(obj);
                if ((a == nullptr) || a->is_finished()) {
                  return Subscription{};
                }
                return a->finished_event().Subscribe(std::move(on_finished));
              },
          };

          return StageFatPtr{.vtable = &vtable, .obj = action};
        }} {}

  Subscription RunStage(FinishedHandler&& on_finished) {
    fat_ptr = factory_();
    return fat_ptr->finished_event(std::move(on_finished));
  }

  void Stop() {
    if (!fat_ptr) {
      return;
    }
    fat_ptr->stop();
  }

 private:
  Factory factory_;
  std::optional<StageFatPtr> fat_ptr;
};

template <typename T, typename Func>
struct ActionStageRunner;

template <Finishable T, typename Func>
struct ActionStageRunner<T&, Func> {
  T* operator()() { return &std::invoke(func_); }
  Func func_;
};

template <Finishable T, typename Func>
struct ActionStageRunner<T*, Func> {
  T* operator()() { return std::invoke(func_); }
  Func func_;
};

template <typename Func,
          typename Res_ = std::invoke_result_t<std::decay_t<Func>>>
  requires(Finishable<std::remove_pointer_t<std::decay_t<Res_>>>)
constexpr auto Stage(Func&& f) noexcept {
  return ActionStageRunner<Res_, Func>{.func_ = std::forward<Func>(f)};
}
}  // namespace actions_queue_internal

using actions_queue_internal::Stage;

class ActionsQueue final {
 public:
  using StageFactory = actions_queue_internal::StageFactory;

  template <actions_queue_internal::StageRunner T>
  void Push(T&& stage) {
    queue_.emplace(std::forward<T>(stage));
    // if no current running stage
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
  }

 private:
  void RunStage() {
    while (!queue_.empty()) {
      running_stage_ = std::move(queue_.front());
      queue_.pop();
      stage_sub_ = running_stage_->RunStage([this]() {
        running_stage_.reset();
        RunStage();
      });
      // if waiting for action is finished
      if (stage_sub_) {
        break;
      }
      running_stage_.reset();
    }
  }

  std::queue<StageFactory> queue_;
  std::optional<StageFactory> running_stage_;
  Subscription stage_sub_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTIONS_QUEUE_H_

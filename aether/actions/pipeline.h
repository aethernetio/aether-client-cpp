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

#ifndef AETHER_ACTIONS_PIPELINE_H_
#define AETHER_ACTIONS_PIPELINE_H_

#include <tuple>
#include <cstdint>
#include <utility>
#include <functional>

#include "aether/type_traits.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"
#include "aether/actions/action_event_status.h"

namespace ae {
namespace pipeline_internal {
template <std::size_t I>
struct Index {
  static constexpr std::size_t value = I;
};

template <typename Func, std::size_t... Is>
void DispatchIndexImpl(Func&& func, std::size_t index,
                       std::index_sequence<Is...>) {
  (  //
      std::invoke([&]() {
        if (index == Is) {
          std::forward<Func>(func)(Index<Is>{});
        }
      }),
      ...);
}

template <std::size_t N, typename Func>
void DispatchIndex(Func&& func, std::size_t index) {
  DispatchIndexImpl(std::forward<Func>(func), index,
                    std::make_index_sequence<N>{});
}

}  // namespace pipeline_internal

/**
 * \brief Pipeline is an infrastructure action calls other actions in sequence
 * while all of them are successful.
 */
class IPipeline : public Action<IPipeline> {
 public:
  using Index = std::uint8_t;
  using Action::Action;

  virtual UpdateStatus Update() = 0;
  virtual Index index() const = 0;
  virtual Index count() const = 0;
  virtual void Stop() = 0;
};

/**
 * \brief The pipeline context.
 * Stores all the stages accessible by index.
 * Pipeline context is optionally passed to all stage.Run to use by the stages.
 */
template <typename... TStages>
class PipelineContext {
 public:
  static constexpr std::size_t kNumStages = sizeof...(TStages);

  explicit PipelineContext(TStages&&... stages)
      : stages_(std::forward<TStages>(stages)...) {}

  template <typename TFunc>
  void DispatchByIndex(IPipeline::Index index, TFunc func) {
    pipeline_internal::DispatchIndex<kNumStages>(
        [&](auto index) { func(GetStage<decltype(index)::value>()); },
        static_cast<std::size_t>(index));
  }

  template <std::size_t Index>
  auto& GetStage() {
    static_assert(Index < kNumStages);
    return std::get<Index>(stages_);
  }

 private:
  std::tuple<TStages...> stages_;
};

/**
 * \brief Templated pipeline implementation.
 * Each TStages should be a Stage runner - something with Run method and
 * operator-> returns ActionPtr.
 * Stages runs in sequence while all of them are successful.
 * On Error or Stop, the pipeline stops.
 */
template <typename... TStages>
class Pipeline final : public IPipeline {
  enum class State : std::uint8_t {
    kStart,
    kRunning,
    kStopped,
    kFailed,
    kCompleted
  };

 public:
  using BaseAction = Action<IPipeline>;
  using Context = PipelineContext<TStages...>;

  explicit Pipeline(ActionContext action_context, TStages&&... args)
      : IPipeline{action_context},
        pipeline_context_{std::forward<TStages>(args)...},
        state_{State::kStart} {}

  UpdateStatus Update() override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kStart:
          Start();
          break;
        case State::kRunning:
          break;
        case State::kCompleted:
          return UpdateStatus::Result();
        case State::kStopped:
          return UpdateStatus::Stop();
        case State::kFailed:
          return UpdateStatus::Error();
      }
    }
    return {};
  }

  Index index() const override { return stage_index_; }
  Index count() const override { return Context::kNumStages; }

  void Stop() override {
    if (state_ != State::kRunning) {
      state_ = State::kStopped;
      BaseAction::Trigger();
      return;
    }
    StopStage();
  }

 private:
  void Start() {
    stage_index_ = 0;
    state_ = State::kRunning;
    RunStage(stage_index_);
  }

  void NextStage() {
    if (++stage_index_ == Context::kNumStages) {
      state_ = State::kCompleted;
      BaseAction::Trigger();
      return;
    }
    RunStage(stage_index_);
  }

  void RunStage(Index index) {
    pipeline_context_.DispatchByIndex(
        index, [this](auto&& stage) { this->RunStage(stage); });
  }

  template <typename TStage>
  void RunStage(TStage& stage) {
    stage.Run();
    StageSubscribe(stage.action());
  }

  template <typename TAction>
  void StageSubscribe(ActionPtr<TAction>& action) {
    if (!action) {
      state_ = State::kFailed;
      BaseAction::Trigger();
      return;
    }
    stage_sub_ = action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[this]() { NextStage(); }},
        OnError{[this]() {
          state_ = State::kFailed;
          BaseAction::Trigger();
        }},
        OnStop{[this]() {
          state_ = State::kStopped;
          BaseAction::Trigger();
        }},
    });
  }

  void StopStage() {
    pipeline_context_.DispatchByIndex(
        stage_index_, [this](auto& stage) { this->StopStage(stage); });
  }

  template <typename TStage>
  void StopStage(TStage& stage) {
    using ActionType = typename TStage::type;
    if constexpr (ActionStoppable<ActionType>::value) {
      auto action = stage.action();
      if (action) {
        action->Stop();
      } else {
        state_ = State::kStopped;
        BaseAction::Trigger();
      }
    } else {
      stage_sub_.Reset();
      state_ = State::kStopped;
      BaseAction::Trigger();
    }
  }
  Context pipeline_context_;
  Index stage_index_{};
  StateMachine<State> state_;

  Subscription stage_sub_;
};

template <typename... Ts>
Pipeline(ActionContext, Ts&&...) -> Pipeline<Ts...>;

/**
 * \brief Stage runners are helper types to define pipeline stages.
 * Run method is called by Pipeline when selected stage is run.
 */
template <typename...>
class StageRunner;

/**
 * \brief Stage runner based on construction of action T by args
 */
template <typename T, typename... TArgs>
class StageRunner<T, TArgs...> {
 public:
  using type = T;

  explicit StageRunner(TArgs... args)
      : construction_args_{std::forward<TArgs>(args)...} {}

  void Run() {
    action_ = std::apply(
        [&](TArgs&... args) {
          return ActionPtr<type>{std::forward<TArgs>(args)...};
        },
        construction_args_);
  }

  ActionPtr<type>& action() { return action_; }

 private:
  std::tuple<TArgs...> construction_args_;
  ActionPtr<type> action_;
};

/**
 * \brief Stage runner based on construction of action T calling a factory
 * function.
 */
template <typename TFunc>
class StageRunner<TFunc> {
  template <typename T>
  struct ActionType;

  template <typename T>
  struct ActionType<ActionPtr<T>> {
    using type = T;
  };

 public:
  using type =
      typename ActionType<typename FunctionSignature<TFunc>::Ret>::type;

  explicit StageRunner(TFunc&& factory) : factory_(std::move(factory)) {}

  void Run() { action_ = factory_(); }

  ActionPtr<type>& action() { return action_; }

 private:
  TFunc factory_;
  ActionPtr<type> action_;
};

/**
 * \brief Helper functions to declare a stages in easier way.
 */
template <typename T, typename... TArgs>
auto Stage(TArgs&&... args) {
  return StageRunner<T, TArgs...>{std::forward<TArgs>(args)...};
}

template <template <typename...> typename T, typename... TArgs>
auto Stage(TArgs&&... args) {
  using DeducedT = decltype(T(std::forward<TArgs>(args)...));
  return StageRunner<DeducedT, TArgs...>{std::forward<TArgs>(args)...};
}

template <typename TFunc>
auto Stage(TFunc&& func) {
  return StageRunner<TFunc>{std::forward<TFunc>(func)};
}

}  // namespace ae

#endif  // AETHER_ACTIONS_PIPELINE_H_

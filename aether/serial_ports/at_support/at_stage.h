/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_SERIAL_PORTS_AT_SUPPORT_H_
#define AETHER_SERIAL_PORTS_AT_SUPPORT_H_

#include <utility>
#include <variant>
#include <functional>
#include <type_traits>

#include "aether/ae_context.h"
#include "aether/actions/action.h"
#include "aether/executors/executors.h"

namespace ae::at_stage_internal {
template <ex::sender RequestSender>
class AtStageAction final : public Action {
 public:
  AtStageAction(AeContext const& ae_context, RequestSender&& sender)
      : waiter_{ae_context, std::move(sender),
                [&](auto const&...) noexcept { Finish(); }} {}

  AE_CLASS_MOVE_ONLY(AtStageAction)

 private:
  ex::AsyncWaiter<AeContext, RequestSender> waiter_;
};

template <typename Gen>
class AtStageActionRunner {
  struct Generator {
    AeContext ae_context;
    Gen gen;
  };

 public:
  using SenderType = std::invoke_result_t<Gen>;
  using ActionType = AtStageAction<SenderType>;

  AtStageActionRunner(AeContext const& ae_context, Gen&& generator)
      : storage_{std::in_place_type_t<Generator>{}, ae_context,
                 std::move(generator)} {}

  AtStageActionRunner(AtStageActionRunner const&) = delete;

  AtStageActionRunner(AtStageActionRunner&& other) noexcept
      : storage_{std::move(std::get<Generator>(other.storage_))} {}

  ActionType* operator()() {
    auto& generator = std::get<Generator>(storage_);
    storage_.template emplace<ActionType>(generator.ae_context,
                                          std::invoke(generator.gen));
    return &std::get<ActionType>(storage_);
  }

 private:
  std::variant<Generator, ActionType> storage_;
};
}  // namespace ae::at_stage_internal

namespace ae::at {
/**
 * \brief The stage command for using in ActionQueue::Push method.
 * Gen - generator is a callable returns ex::sender
 * Produces ATStageActionRunner which creates AtStageAction and waits untile
 * sender is done.
 */
template <typename Gen>
  requires(ex::sender<std::invoke_result_t<std::decay_t<Gen>>>)
decltype(auto) Stage(AeContext const& ae_context, Gen&& generator) {
  return ae::at_stage_internal::AtStageActionRunner{
      ae_context, std::forward<Gen>(generator)};
}
}  // namespace ae::at

#endif  //  AETHER_SERIAL_PORTS_AT_SUPPORT_H_

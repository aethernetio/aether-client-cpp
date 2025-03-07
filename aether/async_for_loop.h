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

#ifndef AETHER_ASYNC_FOR_LOOP_H_
#define AETHER_ASYNC_FOR_LOOP_H_

#include <utility>
#include <optional>
#include <functional>

#include "aether/common.h"
#include "aether/type_traits.h"

namespace ae {
template <typename T>
class AsyncForLoop {
 public:
  using InitFunc = std::function<void()>;
  using PredicateFunc = std::function<bool()>;
  using IncrementFunc = std::function<void()>;
  using BodyReturnType =
      std::conditional_t<IsOptionalLike<T>::value, T, std::optional<T>>;
  using BodyFunc = std::function<T()>;

  // construct based on TClass with AsyncLoop protocol support and custom body
  template <typename TClass,
            typename _ = std::void_t<decltype(std::declval<TClass>().Init()),
                                     decltype(std::declval<TClass>().Next()),
                                     decltype(std::declval<TClass>().End())>>
  static auto Construct(TClass& instance, BodyFunc body) {
    auto* i = &instance;
    return AsyncForLoop{
        [i]() { i->Init(); },
        [i]() { return !i->End(); },
        [i]() { i->Next(); },
        std::move(body),
    };
  }

  // construct based on TClass with AsyncLoop protocol support
  template <typename TClass,
            typename _ = std::void_t<decltype(std::declval<TClass>().Init()),
                                     decltype(std::declval<TClass>().Next()),
                                     decltype(std::declval<TClass>().End()),
                                     decltype(std::declval<TClass>().Get())>>
  static auto Construct(TClass& instance) {
    auto* i = &instance;
    return AsyncForLoop{
        [i]() { i->Init(); },
        [i]() { return !i->End(); },
        [i]() { i->Next(); },
        [i]() { return i->Get(); },
    };
  }

  AsyncForLoop(InitFunc init, PredicateFunc predicate, IncrementFunc increment,
               BodyFunc&& body)
      : init_{std::move(init)},
        predicate_{std::move(predicate)},
        increment_{std::move(increment)},
        body_{std::move(body)} {
    Init();
  }

  AE_CLASS_MOVE_ONLY(AsyncForLoop);

  BodyReturnType Update() {
    // call body_ until it is true, otherwise increment
    if (is_not_end_ && Predicate()) {
      auto res = body_();
      Increment();
      if constexpr (IsOptional<BodyReturnType>::value) {
        return std::optional{std::move(res)};
      } else {
        return res;
      }
    }
    return {};
  }

 private:
  void Init() {
    init_();
    Predicate();
  }

  bool Predicate() {
    is_not_end_ = predicate_();
    return is_not_end_;
  }

  void Increment() { increment_(); }

  InitFunc init_;
  PredicateFunc predicate_;
  IncrementFunc increment_;
  BodyFunc body_;
  bool is_not_end_;
};
}  // namespace ae

#endif  // AETHER_ASYNC_FOR_LOOP_H_

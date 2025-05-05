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

#ifndef AETHER_STREAM_API_DELEGATE_GATE_H_
#define AETHER_STREAM_API_DELEGATE_GATE_H_

#include <variant>
#include <functional>

#include "aether/events/delegate.h"

namespace ae {
namespace delegate_gate_internal {
template <typename T, typename Enable = void>
struct HasSize : std::false_type {};

template <typename T>
struct HasSize<T, std::void_t<decltype(std::size(std::declval<T>()))>>
    : std::true_type {};
}  // namespace delegate_gate_internal

template <typename TIn, typename TOut = TIn>
class DelegateWriteInGate {
 public:
  using DelegateType = Delegate<TOut(TIn&& in_data)>;
  using FunctionalType = std::function<TOut(TIn&& in_data)>;

  explicit DelegateWriteInGate(DelegateType&& delegate)
      : delegate_(std::move(delegate)) {}
  explicit DelegateWriteInGate(FunctionalType&& delegate)
      : delegate_(std::move(delegate)) {}

  TOut WriteIn(TIn&& in_data) {
    return std::visit(
        [&](auto& delegate) { return delegate(std::move(in_data)); },
        delegate_);
  }

  std::size_t Overhead() const {
    if constexpr (delegate_gate_internal::HasSize<TIn>::value &&
                  delegate_gate_internal::HasSize<TOut>::value) {
      auto test_data = TIn{};
      auto size_before = std::size(test_data);
      auto test =
          const_cast<DelegateWriteInGate*>(this)->WriteIn(std::move(test_data));
      return std::size(test) - size_before;
    } else {
      return 0;
    }
  }

 private:
  std::variant<DelegateType, FunctionalType> delegate_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_DELEGATE_GATE_H_

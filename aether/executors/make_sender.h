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

#ifndef AETHER_EXECUTORS_MAKE_SENDER_H_
#define AETHER_EXECUTORS_MAKE_SENDER_H_

#include <functional>

#include <exec/create.hpp>
#include <stdexec/execution.hpp>

namespace ae::ex {
namespace make_sender_internal {
template <typename Func, typename... Sigs>
class GenSender {
 public:
  template <typename R>
  struct Operation {
    void start() & noexcept {
      if constexpr (std::invocable<Func, R&&>) {
        std::invoke(f_, std::move(r_));
      } else {
        std::invoke(f_, r_);
      }
    }

    R r_;
    Func f_;
  };

  using sender_concept = stdexec::sender_t;

  template <typename...>
  static consteval auto get_completion_signatures() noexcept
      -> stdexec::completion_signatures<Sigs...> {
    return {};
  }

  explicit constexpr GenSender(Func&& f) noexcept : f_{std::move(f)} {}

  template <stdexec::receiver R>
    requires(std::invocable<Func, R &&> || std::invocable<Func, R&>)
  constexpr auto connect(R&& r) && noexcept {
    return Operation{.r_ = std::forward<R>(r), .f_ = std::move(f_)};
  }

 private:
  Func f_;
};

template <typename... Sigs>
struct MakeSender {
  template <typename F>
  constexpr auto operator()(F&& f) const noexcept {
    return GenSender<std::decay_t<F>, Sigs...>{std::forward<F>(f)};
  };
};
}  // namespace make_sender_internal

template <typename... Sigs>
static constexpr inline auto make_sender =
    make_sender_internal::MakeSender<Sigs...>{};

using experimental::execution::create;
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_MAKE_SENDER_H_

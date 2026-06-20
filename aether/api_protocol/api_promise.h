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

#ifndef AETHER_API_PROTOCOL_API_PROMISE_H_
#define AETHER_API_PROTOCOL_API_PROMISE_H_

#include <cstdint>

#include "aether-miscpp/types/result.h"
#include "aether/events/events.h"
#include "aether/api_protocol/request_id.h"

namespace ae {
namespace api_promise_action_internal {
struct Empty {};
};  // namespace api_promise_action_internal

template <typename Value, typename Error = std::uint32_t>
class ApiPromise {
 public:
  using value_type = Value;
  using error_type = Error;
  using result_type = Result<value_type, error_type>;

  constexpr explicit ApiPromise(RequestId req_id) noexcept
      : request_id_{req_id} {}

  constexpr void SetValue(value_type&& val) noexcept {
    event_.Emit(result_type{Ok{std::move(val)}});
  }

  constexpr void SetError(error_type&& err) noexcept {
    event_.Emit(result_type{Error{std::move(err)}});
  }

  constexpr RequestId request_id() const { return request_id_; }

  template <typename Fn>
  constexpr decltype(auto) Subscribe(Fn&& fn) noexcept {
    return EventSubscriber{event_}.Subscribe(std::forward<Fn>(fn));
  }

 private:
  RequestId request_id_;
  Event<void(result_type&& res)> event_;
};

template <typename Error>
class ApiPromise<void, Error> {
 public:
  using value_type = api_promise_action_internal::Empty;
  using error_type = Error;
  using result_type = Result<value_type, error_type>;

  constexpr explicit ApiPromise(RequestId req_id) noexcept
      : request_id_{req_id} {}

  constexpr void SetValue() noexcept {
    event_.Emit(result_type{api_promise_action_internal::Empty{}});
  }

  constexpr void SetError(error_type&& err) noexcept {
    event_.Emit(result_type{Error{std::move(err)}});
  }

  constexpr RequestId request_id() const { return request_id_; }

  template <typename Fn>
  [[nodiscard]] constexpr decltype(auto) Subscribe(Fn&& fn) noexcept {
    return EventSubscriber{event_}.Subscribe(std::forward<Fn>(fn));
  }

 private:
  RequestId request_id_;
  Event<void(result_type&& res)> event_;
};

}  // namespace ae
#endif  // AETHER_API_PROTOCOL_API_PROMISE_H_

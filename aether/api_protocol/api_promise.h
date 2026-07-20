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
#include <type_traits>
#include <utility>

#include "aether-miscpp/meta/ignore_t.h"
#include "aether-miscpp/types/result.h"
#include "aether/api_protocol/api_pack_parser.h"
#include "aether/api_protocol/protocol_context.h"
#include "aether/api_protocol/request_id.h"
#include "aether/events/events.h"

namespace ae {
namespace api_promise_internal {
template <typename Value, typename Error>
class PendingResponseEntry final : public ProtocolContext::PendingResponse {
 public:
  using value_type = std::conditional_t<std::is_void_v<Value>, Ignore, Value>;
  using result_type = Result<value_type, Error>;

  void OnResult(ApiParser& parser) override {
    if constexpr (std::is_void_v<Value>) {
      event.Emit(result_type{Ok{kIgnore}});
    } else {
      event.Emit(result_type{Ok{parser.template Extract<Value>()}});
    }
  }

  void OnError(std::uint8_t, std::int32_t error_code) override {
    event.Emit(result_type{Error{static_cast<Error>(error_code)}});
  }

  void OnEvicted() override {
    event.Emit(result_type{Error{static_cast<Error>(-1)}});
  }

  Event<void(result_type&& res)> event;
};
}  // namespace api_promise_internal

template <typename Value, typename Error = std::int32_t>
class ApiPromise {
 public:
  using error_type = Error;
  using value_type = std::conditional_t<std::is_void_v<Value>, Ignore, Value>;
  using result_type = Result<value_type, error_type>;

  ApiPromise(ProtocolContext& protocol_context, RequestId req_id) noexcept
      : protocol_context_{&protocol_context}, request_id_{req_id} {}

  constexpr RequestId request_id() const { return request_id_; }

  template <typename Fn>
  constexpr decltype(auto) Subscribe(Fn&& fn) {
    using Entry = api_promise_internal::PendingResponseEntry<Value, Error>;
    auto& entry =
        protocol_context_->template CreatePendingResponse<Entry>(request_id_);
    return EventSubscriber{entry.event}.Subscribe(std::forward<Fn>(fn));
  }

 private:
  ProtocolContext* protocol_context_;
  RequestId request_id_;
};

}  // namespace ae
#endif  // AETHER_API_PROTOCOL_API_PROMISE_H_

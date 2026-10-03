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

#ifndef AETHER_API_PROTOCOL_DETAILS_API_PROMISE_H_
#define AETHER_API_PROTOCOL_DETAILS_API_PROMISE_H_

#include <cassert>
#include <cstdint>
#include <utility>

#include "aether-miscpp/meta/ignore_t.h"
#include "aether-miscpp/types/result.h"

#include "aether/events/events.h"

#include "aether/api_protocol/details/packet_reader.h"
#include "aether/api_protocol/details/request_id.h"

namespace ae {

/**
 * \brief Base future class.
 * This could be stored in protocol context and OnResult, OnError, OnEvicted
 * invoked on different protocol events.
 */
class IApiFuture {
 public:
  virtual ~IApiFuture() = default;

  virtual void OnResult(PacketReader& reader) = 0;
  virtual void OnError(std::uint8_t, std::int32_t error_code) = 0;
  virtual void OnEvicted() = 0;
};

/**
 * \brief Actual future class, it knows the actual value type.
 * Also it's possible to set value directly via OnResult(V&& v).
 * This allows to use it also on server side.
 */
template <typename V>
class ApiFuture final : public IApiFuture {
 public:
  using result_type = Result<V, std::int32_t>;
  using ResultEvent = Event<void(result_type)>;

  explicit ApiFuture(EventContext auto const& context, RequestId req_id)
      : request_id{req_id}, result_event{context} {}

  void OnResult(PacketReader& reader) override {
    auto value = reader.template TryExtract<V>();
    if (!value) {
      reader.Cancel();
      result_event.Emit(Error{std::int32_t{-2}});
      return;
    }
    OnResult(std::move(*value));
  }

  void OnResult(V&& v) { result_event.Emit(Ok{std::move(v)}); }

  void OnError(std::uint8_t, std::int32_t error_code) override {
    result_event.Emit(Error{error_code});
  }

  void OnEvicted() override {
    result_event.Emit(Error{static_cast<std::int32_t>(-1)});
  }

  RequestId request_id;
  ResultEvent result_event;
};

/**
 * \brief Sepcialization for void type.
 * Void type does not require to use parser to extract any value.
 */
template <>
class ApiFuture<void> final : public IApiFuture {
 public:
  using result_type = Result<Ignore, std::int32_t>;
  using ResultEvent = Event<void(result_type)>;

  explicit ApiFuture(EventContext auto const& context, RequestId req_id)
      : request_id{req_id}, result_event{context} {}

  void OnResult(PacketReader&) override { OnResult(); }

  void OnResult() { result_event.Emit(result_type{Ok{kIgnore}}); }

  void OnError(std::uint8_t, std::int32_t error_code) override {
    result_event.Emit(Error{error_code});
  }

  void OnEvicted() override { result_event.Emit(Error{std::int32_t{-1}}); }

  RequestId request_id;
  ResultEvent result_event;
};

/**
 * \brief Transient, non-owning request handle for subscribing during setup.
 *
 * ApiPromise is intended only for the request setup path, primarily to call
 * Subscribe(). Do not store it in objects or otherwise retain it. Before the
 * promise, future, or ProtocolContext lifetime can end, callers must create
 * and retain Subscription or MultiSubscription RAII owners for any callbacks.
 *
 * ApiPromise does not cancel its request when discarded. Do not use or retain
 * it after the associated operation completes, is evicted, or its
 * ProtocolContext is destroyed. Subscription tokens returned by Subscribe()
 * follow the Event ownership rules: transfer each token to exactly one
 * Subscription or MultiSubscription.
 */
template <typename V>
class ApiPromise {
 public:
  ApiPromise() = default;
  ApiPromise(ApiFuture<V>& future) noexcept  // NOLINT(*explicit*)
      : future_{&future} {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return future_ != nullptr;
  }

  constexpr RequestId request_id() const {
    assert(future_ != nullptr && "Get request id from empty future");
    return future_->request_id;
  }

  template <typename F>
  constexpr decltype(auto) Subscribe(F&& f) {
    assert(future_ != nullptr && "Subscribe to empty future");
    return future_->result_event.Subscribe(std::forward<F>(f));
  }

 private:
  ApiFuture<V>* future_{};
};

}  // namespace ae
#endif  // AETHER_API_PROTOCOL_DETAILS_API_PROMISE_H_

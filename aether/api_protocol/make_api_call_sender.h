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

#ifndef AETHER_API_PROTOCOL_MAKE_API_CALL_SENDER_H_
#define AETHER_API_PROTOCOL_MAKE_API_CALL_SENDER_H_

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "aether/events/events.h"
#include "aether/executors/executors.h"
#include "aether/write_action/write_action.h"

#include "aether/api_protocol/details/api_context.h"
#include "aether/api_protocol/details/api_promise.h"

namespace ae {
struct WriteFailed {};

namespace make_api_call_sender_internal {

template <typename T>
inline constexpr bool kIsApiPromise = false;

template <typename T>
inline constexpr bool kIsApiPromise<ApiPromise<T>> = true;

template <typename T>
concept ApiPromiseReturn = kIsApiPromise<std::remove_cvref_t<T>>;

template <typename Fn, typename Api>
concept ApiCall = requires(Fn& fn, ApiContext<Api>& api_context) {
  { std::invoke(fn, api_context) } -> ApiPromiseReturn;
};

template <typename T>
struct CallTraits;

template <typename T>
struct CallTraits<ApiPromise<T>> {
  using CompletionSignatures = ex::completion_signatures<
      ex::set_value_t(T), ex::set_error_t(std::int32_t),
      ex::set_error_t(WriteFailed), ex::set_stopped_t()>;
  static constexpr bool kHasValue = true;
};

template <>
struct CallTraits<ApiPromise<void>> {
  using CompletionSignatures = ex::completion_signatures<
      ex::set_value_t(), ex::set_error_t(std::int32_t),
      ex::set_error_t(WriteFailed), ex::set_stopped_t()>;
  static constexpr bool kHasValue = false;
};

template <typename Api, typename Stream, typename Fn, typename Return,
          ex::receiver Receiver>
class Operation final {
  using Traits = CallTraits<Return>;

 public:
  template <typename R>
  Operation(R&& receiver, Api* api, ProtocolContext* protocol_context,
            Stream* stream, Fn&& fn) noexcept
      : receiver_{std::forward<R>(receiver)},
        api_{api},
        protocol_context_{protocol_context},
        stream_{stream},
        fn_{std::move(fn)} {}

  /**
   * Starts this lvalue-qualified stdexec operation state.
   *
   * stdexec calls start() to synchronously build the API context and invoke
   * the callable. An empty promise completes the receiver with set_stopped
   * synchronously and does not write. Otherwise, start() subscribes to the
   * promise, packs and writes the request, subscribes to write status, and
   * returns.
   *
   * The caller/system must ensure that this operation is started once and
   * remains alive until completion. For a non-empty promise, promise and write
   * callbacks must be delivered asynchronously and serialized; terminal
   * callbacks must not be concurrent or reentrant. Under this contract,
   * surrounding event/write integration must deliver at most one terminal
   * promise or write callback for the operation; exactly-once terminal
   * delivery is a precondition because this simplified implementation
   * intentionally has no completed_ guard.
   *
   * Under this contract,
   * terminal callbacks reset subscriptions before completing the receiver, and
   * no operation member may be accessed afterward.
   */
  void start() & noexcept {
    auto api_context = protocol_context_ == nullptr
                           ? ApiContext<Api>{*api_}
                           : ApiContext<Api>{*api_, *protocol_context_};
    auto promise = std::invoke(fn_, api_context);
    if (!promise) {
      ex::set_stopped(std::move(receiver_));
      return;
    }

    result_subscription_ = promise.Subscribe([this](auto&& result) noexcept {
      this->OnPromiseResult(std::forward<decltype(result)>(result));
    });
    auto& write_action = stream_->Write(std::move(api_context).Pack());
    write_subscription_ = write_action.status_event().Subscribe(
        [this](WriteAction::Status status) noexcept {
          this->OnWriteStatus(status);
        });
  }

 private:
  template <typename Result>
  void OnPromiseResult(Result&& result) noexcept {
    ResetSubscriptions();
    if (result) {
      if constexpr (Traits::kHasValue) {
        ex::set_value(std::move(receiver_),
                      std::forward<Result>(result).value());
      } else {
        ex::set_value(std::move(receiver_));
      }
      return;
    }

    ex::set_error(std::move(receiver_), std::forward<Result>(result).error());
  }

  void OnWriteStatus(WriteAction::Status status) noexcept {
    switch (status) {
      case WriteAction::Status::kSuccess:
        return;
      case WriteAction::Status::kFail:
        ResetSubscriptions();
        ex::set_error(std::move(receiver_), WriteFailed{});
        return;
      case WriteAction::Status::kStop:
        ResetSubscriptions();
        ex::set_stopped(std::move(receiver_));
        return;
    }
  }

  void ResetSubscriptions() noexcept {
    result_subscription_.Reset();
    write_subscription_.Reset();
  }

  Receiver receiver_;
  Api* api_;
  ProtocolContext* protocol_context_;
  Stream* stream_;
  Fn fn_;
  Subscription result_subscription_;
  Subscription write_subscription_;
};

template <typename Api, typename Stream, typename Fn>
  requires ApiCall<Fn, Api>
class Sender {
 public:
  using sender_concept = ex::sender_t;
  using Return =
      std::remove_cvref_t<std::invoke_result_t<Fn&, ApiContext<Api>&>>;

  template <typename...>
  static consteval auto get_completion_signatures() noexcept {
    return typename CallTraits<Return>::CompletionSignatures{};
  }

  Sender(Api& api, Stream& stream, Fn&& fn) noexcept
      : api_{&api},
        protocol_context_{nullptr},
        stream_{&stream},
        fn_{std::move(fn)} {}
  Sender(Api& api, ProtocolContext& protocol_context, Stream& stream,
         Fn&& fn) noexcept
      : api_{&api},
        protocol_context_{&protocol_context},
        stream_{&stream},
        fn_{std::move(fn)} {}

  template <typename Receiver>
  auto connect(Receiver&& receiver) && noexcept {
    return Operation<Api, Stream, Fn, Return, std::decay_t<Receiver>>{
        std::forward<Receiver>(receiver), api_, protocol_context_, stream_,
        std::move(fn_)};
  }

 private:
  Api* api_;
  ProtocolContext* protocol_context_;
  Stream* stream_;
  Fn fn_;
};

struct MakeApiCall {
  template <typename Api, typename Stream, typename Fn>
  auto operator()(Api& api, Stream& stream, Fn&& fn) const noexcept
      -> Sender<Api, Stream, std::decay_t<Fn>> {
    return Sender<Api, Stream, std::decay_t<Fn>>{api, stream,
                                                 std::forward<Fn>(fn)};
  }

  template <typename Api, typename Stream, typename Fn>
  auto operator()(Api& api, ProtocolContext& protocol_context, Stream& stream,
                  Fn&& fn) const noexcept
      -> Sender<Api, Stream, std::decay_t<Fn>> {
    return Sender<Api, Stream, std::decay_t<Fn>>{api, protocol_context, stream,
                                                 std::forward<Fn>(fn)};
  }
};

}  // namespace make_api_call_sender_internal

inline constexpr make_api_call_sender_internal::MakeApiCall make_api_call{};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_MAKE_API_CALL_SENDER_H_

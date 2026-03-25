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

#include <utility>
#include <cassert>

#include "aether/actions/action.h"
#include "aether/executors/executors.h"
#include "aether/api_protocol/api_context.h"
#include "aether/events/event_subscription.h"

namespace ae {
struct WriteFailed {};

namespace make_api_call_internal {
template <typename R>
struct OpBase {
  constexpr explicit OpBase(R&& r) noexcept : recv{std::move(r)} {}

  virtual void Reset() noexcept {}
  virtual bool is_reset() const noexcept { return false; }

  R recv;

 protected:
  ~OpBase() = default;
};

template <typename R>
struct Receiver {
  using receiver_concept = ex::receiver_t;

  constexpr void set_value(auto&&... v) && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      ex::set_value(std::move(op->recv), std::forward<decltype(v)>(v)...);
    }
  }
  constexpr void set_error(auto&& e) && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      ex::set_error(std::move(op->recv), std::forward<decltype(e)>(e));
    }
  }
  constexpr void set_stopped() && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      ex::set_stopped(std::move(op->recv));
    }
  }
  constexpr auto get_env() noexcept {
    assert(!op->is_reset());
    return ex::get_env(op->recv);
  }

  OpBase<R>* op;
};

template <typename Api, typename Stream, typename Fn, ex::receiver R>
class Operation final : public OpBase<R> {
 public:
  using receiver = Receiver<R>;

  constexpr Operation(R&& recv, Api* api, Stream* stream, Fn&& fn) noexcept
      : OpBase<R>{std::move(recv)},
        api_{api},
        stream_{stream},
        fn_{std::move(fn)} {}

  constexpr void start() noexcept {
    // prepare api call by user's function
    auto api_context = ApiContext<Api>{*api_};
    // though recv_ moved inside the function, it's possible that the function
    // does not move it inside so we keep store recv_ as a member to ensure it
    // live enough
    recv_.emplace(this);
    if constexpr (std::is_invocable_v<Fn, ApiContext<Api>&, receiver&&>) {
      std::invoke(fn_, api_context, std::move(recv_.value()));
    } else {
      static_assert(std::is_invocable_v<Fn, ApiContext<Api>&, receiver&>,
                    "Fn must be invocable with ApiContext<Api>& and receiver&");
      std::invoke(fn_, api_context, recv_.value());
    }
    // write the result to the stream
    auto stream_action = stream_->Write(std::move(api_context).Pack());
    // if stream is failed, the api call is also failed
    sub_ = stream_action->StatusEvent().Subscribe(ActionHandler{
        OnResult{[]() noexcept {
          /* do nothing */
        }},
        OnError{[&]() noexcept {
          Reset();
          ex::set_error(std::move(OpBase<R>::recv), WriteFailed{});
        }},
        OnStop{[&]() noexcept {
          Reset();
          ex::set_stopped(std::move(OpBase<R>::recv));
        }},
    });
  }

  void Reset() noexcept override { sub_.Reset(); }
  bool is_reset() const noexcept override { return !!sub_; }

 private:
  Api* api_;
  Stream* stream_;
  Fn fn_;
  Subscription sub_;
  std::optional<receiver> recv_;
};

template <typename Completions, typename Api, typename Stream, typename Fn>
class Sender {
 public:
  using sender_concept = ex::sender_t;

  template <typename...>
  static consteval auto get_completion_signatures() noexcept {
    return ex::__transform_completion_signatures(
        Completions{}, ex::__keep_completion<ex::set_value_t>{},
        ex::__keep_completion<ex::set_error_t>{},
        ex::__keep_completion<ex::set_stopped_t>{},
        ex::completion_signatures<ex::set_error_t(WriteFailed)>{});
  }

  constexpr Sender(Api& api, Stream& stream, Fn&& fn) noexcept
      : api_{&api}, stream_{&stream}, fn_{std::move(fn)} {}

  constexpr auto connect(ex::receiver auto&& recv) noexcept {
    return Operation<Api, Stream, Fn, std::decay_t<decltype(recv)>>{
        std::forward<decltype(recv)>(recv),
        api_,
        stream_,
        std::move(fn_),
    };
  }

 private:
  Api* api_;
  Stream* stream_;
  Fn fn_;
};

template <typename... Sigs>
struct MakeApiCall {
  template <typename Api, typename Stream, typename Fn>
  constexpr auto operator()(Api& api, Stream& stream, Fn&& fn) const noexcept {
    return Sender<ex::completion_signatures<Sigs...>, Api, Stream,
                  std::decay_t<Fn>>{
        api,
        stream,
        std::forward<decltype(fn)>(fn),
    };
  }
};

}  // namespace make_api_call_internal

template <typename... Sigs>
static constexpr inline auto make_api_call =
    make_api_call_internal::MakeApiCall<Sigs...>{};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_MAKE_API_CALL_SENDER_H_

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

#ifndef AETHER_API_PROTOCOL_API_PROMISE_ACTION_H_
#define AETHER_API_PROTOCOL_API_PROMISE_ACTION_H_

#include "aether/actions/action_ptr.h"
#include "aether/actions/promise_action.h"
#include "aether/actions/action_context.h"

#include "aether/api_protocol/request_id.h"

namespace ae {
template <typename TValue>
class ApiPromiseAction : public Action<ApiPromiseAction<TValue>> {
 public:
  using BaseAction = Action<ApiPromiseAction<TValue>>;

  ApiPromiseAction(ActionContext action_context, RequestId req_id)
      : BaseAction{action_context},
        promise_{action_context},
        request_id_{req_id} {}

  AE_CLASS_MOVE_ONLY(ApiPromiseAction);

  UpdateStatus Update() { return promise_.Update(); }

  RequestId const& request_id() const { return request_id_; }

  void SetValue(TValue value) { promise_.SetValue(value); }

  void Reject() { promise_.Reject(); }

  TValue& value() { return promise_.value(); }

  TValue const& value() const { return promise_.value(); }

 private:
  PromiseAction<TValue> promise_;
  RequestId request_id_;
};

template <>
class ApiPromiseAction<void> : public Action<ApiPromiseAction<void>> {
 public:
  using BaseAction = Action<ApiPromiseAction<void>>;

  ApiPromiseAction(ActionContext action_context, RequestId req_id)
      : BaseAction{action_context},
        promise_{action_context},
        request_id_{req_id} {}

  AE_CLASS_MOVE_ONLY(ApiPromiseAction);

  UpdateStatus Update() { return promise_.Update(); }

  RequestId const& request_id() const { return request_id_; }

  void SetValue() { promise_.SetValue(); }

  void Reject() { promise_.Reject(); }

 private:
  PromiseAction<void> promise_;
  RequestId request_id_;
};

template <typename T>
using ApiPromisePtr = ActionPtr<ApiPromiseAction<T>>;
}  // namespace ae
#endif  // AETHER_API_PROTOCOL_API_PROMISE_ACTION_H_

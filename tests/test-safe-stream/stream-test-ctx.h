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

#ifndef TESTS_TEST_STREAM_STREAM_TEST_CTX_H
#define TESTS_TEST_STREAM_STREAM_TEST_CTX_H

#include "aether/ae_context.h"

namespace ae {
struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr,
                   [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->sched;
                   },
                   [](void* obj) -> IndexRegistry& {
                     return static_cast<TestContext*>(obj)->registry;
                   }};
    return AeCtx{
        const_cast<TestContext*>(this),  // NOLINT
        &table,
    };
  }

  template <typename... A>
  decltype(auto) Update(A&&... a) {
    return sched.Update(std::forward<A>(a)...);
  }

  TaskScheduler sched;
  IndexRegistry registry;
};
}  // namespace ae

#endif  // TESTS_TEST_STREAM_STREAM_TEST_CTX_H

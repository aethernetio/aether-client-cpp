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

#ifndef AETHER_EXECUTORS_EXECUTORS_H_
#define AETHER_EXECUTORS_EXECUTORS_H_

// IWYU pragma: begin_exports

#include "third_party/libunifex/include/unifex/adapt_stream.hpp"
#include "third_party/libunifex/include/unifex/any_sender_of.hpp"

#include "third_party/libunifex/include/unifex/create.hpp"

#include "third_party/libunifex/include/unifex/for_each.hpp"

#include "third_party/libunifex/include/unifex/just_done.hpp"
#include "third_party/libunifex/include/unifex/just_error.hpp"
#include "third_party/libunifex/include/unifex/just_from.hpp"
#include "third_party/libunifex/include/unifex/just_void_or_done.hpp"

#include "third_party/libunifex/include/unifex/let_done.hpp"
#include "third_party/libunifex/include/unifex/let_error.hpp"
#include "third_party/libunifex/include/unifex/let_value.hpp"
#include "third_party/libunifex/include/unifex/let_value_with.hpp"
#include "third_party/libunifex/include/unifex/let_value_with_stop_source.hpp"
#include "third_party/libunifex/include/unifex/let_value_with_stop_token.hpp"

#include "third_party/libunifex/include/unifex/on.hpp"
#include "third_party/libunifex/include/unifex/on_stream.hpp"

#include "third_party/libunifex/include/unifex/range_stream.hpp"
#include "third_party/libunifex/include/unifex/reduce_stream.hpp"
#include "third_party/libunifex/include/unifex/retry_when.hpp"

#include "third_party/libunifex/include/unifex/sync_wait.hpp"

#include "third_party/libunifex/include/unifex/tag_invoke.hpp"
#include "third_party/libunifex/include/unifex/then.hpp"
#include "third_party/libunifex/include/unifex/then_execute.hpp"
#include "third_party/libunifex/include/unifex/transform_stream.hpp"

#include "third_party/libunifex/include/unifex/when_any.hpp"
#include "third_party/libunifex/include/unifex/when_all.hpp"
#include "third_party/libunifex/include/unifex/when_all_range.hpp"

#include "third_party/libunifex/include/unifex/upon_error.hpp"

// IWYU pragma: end_exports

namespace ae {
namespace ex {
// make a namespace alias for unifex as ae::ex
using namespace unifex;

inline auto make_error() { return std::make_exception_ptr(-1); }

}  // namespace ex
}  // namespace ae

#endif  // AETHER_EXECUTORS_EXECUTORS_H_

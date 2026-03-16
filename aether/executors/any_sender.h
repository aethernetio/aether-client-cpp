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
#ifndef AETHER_EXECUTORS_ANY_SENDER_H_
#define AETHER_EXECUTORS_ANY_SENDER_H_

#include <exception>

#include "third_party/stdexec/include/exec/any_sender_of.hpp"

namespace ae::ex {
// provide the list of signatures in form
// set_value_t(type...), set_error_t(type)
// !Note set_error_t must take one type, while set_value_t may not have any
template <typename... Signs>
using AnySender = typename experimental::execution::any_receiver_ref<
    stdexec::completion_signatures<Signs...,
                                   // add std::exception_ptr by default, because
                                   // it's to easy to add it while building
                                   // senders chain
                                   stdexec::set_error_t(std::exception_ptr)>
    // ~['_']~
    >::template any_sender<>;
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_ANY_SENDER_H_

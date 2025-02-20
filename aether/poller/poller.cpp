/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/poller/poller.h"

namespace ae {
#if defined AE_DISTILLATION
IPoller::IPoller(Domain* domain) : Obj{domain} {}
#endif
IPoller::~IPoller() = default;

[[noreturn]] IPoller::OnPollEventSubscriber no_return_subscriber() {
  std::abort();
}

IPoller::OnPollEventSubscriber IPoller::Add(DescriptorType /* descriptor */) {
  assert(false);
  // this must never invoked
  return no_return_subscriber();
}

void IPoller::Remove(DescriptorType /* descriptor */) { assert(false); }

}  // namespace ae

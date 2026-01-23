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

#include "aether/adapters/adapter.h"

namespace ae {
#ifdef AE_DISTILLATION
Adapter::Adapter(ObjProp prop) : Obj{prop} {}
#endif  // AE_DISTILLATION

Adapter::NewAccessPoint::Subscriber Adapter::new_access_point() {
  return EventSubscriber{new_access_point_event_};
}
}  // namespace ae

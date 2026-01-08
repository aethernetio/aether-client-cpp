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

#ifndef AETHER_POLLER_POLLER_H_
#define AETHER_POLLER_POLLER_H_

#include "aether/obj/obj.h"

namespace ae {
class NativePoller {};

class IPoller : public Obj {
  AE_OBJECT(IPoller, Obj, 0)

 protected:
  IPoller() = default;

 public:
  explicit IPoller(Domain* domain) : Obj{domain} {}

  AE_OBJECT_REFLECT()

  /**
   * \brief Return native poller implementation.
   */
  virtual NativePoller* Native() = 0;
};
}  // namespace ae

#endif  // AETHER_POLLER_POLLER_H_ */

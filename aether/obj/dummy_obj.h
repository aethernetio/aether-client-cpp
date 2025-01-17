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

#ifndef AETHER_OBJ_DUMMY_OBJ_H_
#define AETHER_OBJ_DUMMY_OBJ_H_

#include "aether/obj/obj.h"

namespace ae {
/**
 * \brief Use it to fill the holes in object states configured with different
 * preprocessor directives
 */
class DummyObj : public Obj {
  AE_OBJECT(DummyObj, Obj, 0)
 public:
  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }
};
}  // namespace ae

#endif  // AETHER_OBJ_DUMMY_OBJ_H_
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

#include "aether/common.h"
#include "aether/obj/obj.h"

namespace ae {
/**
 * \brief Use it to fill the holes in object states configured with different
 * preprocessor directives
 */
class DummyObj : public Obj {
  AE_OBJECT(DummyObj, Obj, 0)

  using Obj::Obj;

 public:
  AE_OBJECT_REFLECT()
};

#define AE_DUMMY_OBJ(DERIVED)                                       \
 protected:                                                         \
  friend class ae::Registrar<DERIVED>;                              \
  friend ae::Ptr<DERIVED> ae::MakePtr<DERIVED>();                   \
                                                                    \
 public:                                                            \
  _AE_OBJECT_FIELDS(crc32::from_literal(STR(DERIVED##dummy)).value, \
                    DummyObj::kClassId, 0)                          \
  inline static auto registrar_ =                                   \
      ae::Registrar<DERIVED>(kClassId, kBaseClassId);               \
  using Base = DummyObj;                                            \
  using ptr = ae::ObjPtr<DERIVED>;                                  \
  Base& base_{*this};                                               \
  std::uint32_t GetClassId() const override { return kClassId; }    \
                                                                    \
 public:                                                            \
  using DummyObj::DummyObj;                                         \
  AE_OBJECT_REFLECT()                                               \
 private:

}  // namespace ae

#endif  // AETHER_OBJ_DUMMY_OBJ_H_

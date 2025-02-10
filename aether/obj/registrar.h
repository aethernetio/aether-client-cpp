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

#ifndef AETHER_OBJ_REGISTRAR_H_
#define AETHER_OBJ_REGISTRAR_H_

#include <type_traits>

#include "aether/obj/domain.h"
#include "aether/obj/registry.h"
#include "aether/obj/obj_ptr.h"

namespace ae {
template <class T>
class Registrar {
 public:
  Registrar(std::uint32_t cls_id, std::uint32_t base_id) {
    Registry::RegisterClass(cls_id, base_id,
                            {
                                // create
                                Delegate(MethodPtr<&Create>{}),
                                // load
                                Delegate(MethodPtr<&Load>{}),
                                // save
                                Delegate(MethodPtr<&Save>{})
#ifdef DEBUG
                                    ,
                                std::string{GetTypeName<T>()},
                                cls_id,
                                base_id,
#endif  // DEBUG
                            });
  }

 private:
  // like std::is_default_constructible but with respect to Registrar<-Ojb
  // friendship
  template <typename U, typename _ = void>
  struct IsDefaultConstructible : std::false_type {};
  template <typename U>
  struct IsDefaultConstructible<U, std::void_t<decltype(U())>>
      : std::true_type {};

  static ObjPtr<Obj> Create() {
    static_assert(IsDefaultConstructible<T>::value,
                  "AE_OBJECT class should be default constructible");
    return ObjPtr(MakePtr<T>());
  }
  static ObjPtr<Obj> Load(Domain* domain, ObjPtr<Obj> obj) {
    auto self_ptr = ObjPtr<T>{std::move(obj)};
    domain->Load(*self_ptr.get());
    return self_ptr;
  }
  static void Save(Domain* domain, ObjPtr<Obj> const& obj) {
    auto self_ptr = static_cast<T*>(obj.get());
    domain->Save(*self_ptr);
  }
};
}  // namespace ae
#endif  // AETHER_OBJ_REGISTRAR_H_

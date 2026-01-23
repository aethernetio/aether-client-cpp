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

#include "aether/ptr/ptr.h"
#include "aether/obj/domain.h"
#include "aether/obj/registry.h"
#include "aether/reflect/type_index.h"

namespace ae {
template <class T>
class Registrar {
 public:
  Registrar(std::uint32_t cls_id, std::uint32_t base_id) {
    Registry::GetRegistry().RegisterClass(
        cls_id, base_id,
        {
            Factory::CreateFunc(&Create),
            Factory::LoadFunc(&Load),
            Factory::SaveFunc(&Save),
#ifdef DEBUG
            std::string{reflect::GetTypeName<T>()},
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

  static Ptr<Obj> Create() {
    if constexpr (std::is_abstract_v<T>) {
      assert(false && "Create called on abstract class");
      return {};
    } else {
      static_assert(IsDefaultConstructible<T>::value,
                    "AE_OBJECT class should be default constructible");
      return MakePtr<T>();
    }
  }

  static void Load(DomainGraph* domain_graph, Ptr<Obj>& obj, ObjId obj_id) {
    auto self_ptr = static_cast<T*>(obj.get());
    domain_graph->Load(*self_ptr, obj_id);
  }

  static void Save(DomainGraph* domain_graph, Ptr<Obj> const& obj, ObjId id) {
    auto self_ptr = static_cast<T*>(obj.get());
    domain_graph->Save(*self_ptr, id);
  }
};
}  // namespace ae
#endif  // AETHER_OBJ_REGISTRAR_H_

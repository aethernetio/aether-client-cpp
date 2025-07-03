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

// Copyright 2016 Aether authors. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//   http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef AETHER_OBJ_OBJ_H_
#define AETHER_OBJ_OBJ_H_

#include <array>

#include "aether/config.h"

#include "aether/common.h"
#include "aether/crc.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/domain.h"
#include "aether/obj/registry.h"
#include "aether/obj/registrar.h"
#include "aether/reflect/reflect.h"

namespace ae {
/**
 * \brief Base class for all objects.
 */
class Obj {
  friend class ObjectPtrBase;

 public:
  using CurrentVersion = Version<0>;
  using ptr = ObjPtr<Obj>;

  static constexpr std::uint32_t kClassId = crc32::from_literal("Obj").value;
  static constexpr std::uint32_t kBaseClassId =
      crc32::from_literal("Obj").value;

  static constexpr std::uint32_t kVersion = 0;
  static constexpr CurrentVersion kCurrentVersion{};

  Obj();
  explicit Obj(Domain* domain);
  virtual ~Obj();

  virtual std::uint32_t GetClassId() const;
  virtual void Update(TimePoint current_time);

  ObjId GetId() const;

  AE_REFLECT_MEMBERS(update_time_);

  Domain* domain_{};
  TimePoint update_time_;

 protected:
  ObjId id_;
  ObjFlags flags_;
};

namespace reflect {
template <typename T>
struct ObjectIndex<T, std::enable_if_t<std::is_base_of_v<Obj, T>>> {
  static std::size_t GetIndex(T const* obj) {
    std::array<std::uint8_t, sizeof(std::uint32_t) + sizeof(ObjId::Type)>
        buffer;
    *reinterpret_cast<std::uint32_t*>(buffer.data()) = T::kClassId;
    *reinterpret_cast<ObjId::Type*>(buffer.data() + sizeof(std::uint32_t)) =
        obj->GetId().id();

    return crc32::from_buffer(buffer.data(), buffer.size()).value;
  }
};
}  // namespace reflect

}  // namespace ae

/**
 * \brief Use it inside each derived class to register it with the object system
 */
#define AE_OBJECT(DERIVED, BASE, VERSION)                        \
 protected:                                                      \
  friend class ae::Registrar<DERIVED>;                           \
  friend ae::Ptr<DERIVED> ae::MakePtr<DERIVED>();                \
                                                                 \
 public:                                                         \
  static constexpr std::uint32_t kClassId =                      \
      crc32::from_literal(#DERIVED).value;                       \
  static constexpr std::uint32_t kBaseClassId =                  \
      crc32::from_literal(#BASE).value;                          \
  static constexpr std::uint32_t kVersion = VERSION;             \
  using CurrentVersion = Version<kVersion>;                      \
  static constexpr CurrentVersion kCurrentVersion{};             \
  inline static auto registrar_ =                                \
      ae::Registrar<DERIVED>(kClassId, kBaseClassId);            \
                                                                 \
  using Base = BASE;                                             \
  using ptr = ae::ObjPtr<DERIVED>;                               \
                                                                 \
  Base& base_{*this};                                            \
                                                                 \
  std::uint32_t GetClassId() const override { return kClassId; } \
                                                                 \
 private:                                                        \
  /* add rest class's staff after */

/**
 * \brief Obj class reflection
 */
#define AE_OBJECT_REFLECT(...) \
  AE_REFLECT(AE_REF_BASE(std::decay_t<decltype(base_)>), __VA_ARGS__)

#endif  // AETHER_OBJ_OBJ_H_

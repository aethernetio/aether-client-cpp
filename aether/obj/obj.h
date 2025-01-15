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

#include "aether/config.h"

#include "aether/common.h"
#include "aether/crc.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"
#include "aether/obj/domain.h"

namespace ae {
/**
 * \brief Base class for all objects.
 */
class Obj {
  friend class ObjectPtrBase;

 public:
  using CurrentVersion = Version<0>;
  using ptr = ObjPtr<Obj>;

 public:
  explicit Obj(Domain* domain);
  Obj() = default;
  virtual ~Obj();

  virtual std::uint32_t GetClassId() const;
  virtual void Update(TimePoint p);

  ObjId GetId() const;

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(update_time_);
  }

  static constexpr std::uint32_t kClassId = crc32::from_literal("Obj").value;
  static constexpr std::uint32_t kBaseClassId =
      crc32::from_literal("Obj").value;

  static constexpr std::uint32_t kVersion = 0;
  static constexpr CurrentVersion kCurrentVersion{};

  Domain* domain_{};
  TimePoint update_time_{};

 protected:
  ObjId id_{};
  ObjFlags flags_{};
};

}  // namespace ae

/**
 * \brief Use it inside each derived class to register it with the object system
 */
#define AE_OBJECT(DERIVED, BASE, VERSION)                        \
 protected:                                                      \
  friend struct ae::Registrar<DERIVED>;                          \
  DERIVED() = default;                                           \
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
      ae::Registrar<DERIVED>(kClassId, kBaseClassId, #DERIVED);  \
                                                                 \
  using Base = BASE;                                             \
  using ptr = ae::ObjPtr<DERIVED>;                               \
                                                                 \
  Base* base_ptr_{this};                                         \
                                                                 \
  std::uint32_t GetClassId() const override { return kClassId; } \
                                                                 \
 private:                                                        \
  /*add rest class's staff after*/

#endif  // AETHER_OBJ_OBJ_H_
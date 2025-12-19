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

#include "aether/crc.h"
#include "aether/obj/domain.h"
#include "aether/reflect/reflect.h"

namespace ae {
/**
 * \brief Base class for all objects.
 */
class Obj {
  friend class ObjectPtrBase;

 public:
  using CurrentVersion = Version<0>;

  static constexpr std::uint32_t kClassId = crc32::from_literal("Obj").value;
  static constexpr std::uint32_t kBaseClassId =
      crc32::from_literal("Obj").value;

  static constexpr CurrentVersion kCurrentVersion{};

  Obj();
  explicit Obj(Domain* domain);
  virtual ~Obj();

  virtual std::uint32_t GetClassId() const;

  Domain* domain_{};
};
}  // namespace ae

/**
 * \brief Use it inside each derived class to register it with the object system
 */
#define AE_OBJECT(DERIVED, BASE, VERSION)                                \
 public:                                                                 \
  static constexpr std::uint32_t kClassId =                              \
      crc32::from_literal(#DERIVED).value;                               \
  static constexpr std::uint32_t kBaseClassId =                          \
      crc32::from_literal(#BASE).value;                                  \
  using CurrentVersion = Version<VERSION>;                               \
                                                                         \
  static constexpr auto kTypeName = ae::reflect::GetTypeName<DERIVED>(); \
  static constexpr CurrentVersion kCurrentVersion{};                     \
  using Base = BASE;                                                     \
  Base& base_{*this};                                                    \
                                                                         \
  std::uint32_t GetClassId() const override { return kClassId; }         \
                                                                         \
 private:                                                                \
  /* add rest class's staff after */

#endif  // AETHER_OBJ_OBJ_H_

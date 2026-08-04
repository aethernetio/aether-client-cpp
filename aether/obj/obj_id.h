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

#ifndef AETHER_OBJ_OBJ_ID_H_
#define AETHER_OBJ_OBJ_ID_H_

#include <cstdint>

#include "aether-miscpp/format/format.h"
#include "aether-miscpp/reflect/reflect.h"

namespace ae {

class ObjId {
 public:
  using Type = std::uint32_t;

  static ObjId GenerateUnique();

  constexpr ObjId() noexcept = default;
  // NOLINTNEXTLINE(*explicit*)
  constexpr ObjId(Type i) noexcept : id_{i} {}

  AE_REFLECT_MEMBERS(id_)

  constexpr Type id() const noexcept { return id_; }
  constexpr bool is_valid() const noexcept { return id_ != 0; }

  constexpr bool operator<(ObjId const& i) const noexcept {
    return id_ < i.id_;
  }
  constexpr bool operator!=(ObjId const& i) const noexcept {
    return id_ != i.id_;
  }
  constexpr bool operator==(ObjId const& i) const noexcept {
    return id_ == i.id_;
  }

  constexpr ObjId& operator+=(Type i) noexcept {
    id_ += i;
    return *this;
  }
  constexpr ObjId operator+(Type i) const noexcept { return ObjId{id_ + i}; }

 private:
  Type id_{0};
};

template <>
struct Formatter<ObjId> : public Formatter<typename ObjId::Type> {
  template <typename TStream>
  void Format(ObjId value, FormatContext<TStream>& ctx) const {
    Formatter<typename ObjId::Type>::Format(value.id(), ctx);
  }
};

class ObjFlags {
 public:
  using Type = std::uint8_t;
  // defines for flag values
  // none value
  static constexpr Type kNone = 0x0;
  // The object is not loaded with deserialization. Load method must be
  // used for loading.
  static constexpr Type kUnloadedByDefault = 0x1;
  // current state the object is unloaded
  static constexpr Type kUnloaded = 0x2;

  constexpr ObjFlags() noexcept = default;
  // NOLINTNEXTLINE(*explicit*)
  constexpr ObjFlags(Type v) noexcept : value_(v) {}

  AE_REFLECT_MEMBERS(value_)

  ObjFlags& operator=(Type v) noexcept {
    value_ = v;
    return *this;
  }

  // NOLINTNEXTLINE(*explicit*)
  constexpr operator Type() const noexcept { return value_; }

 private:
  Type value_ = kNone;
};

template <>
struct Formatter<ObjFlags> : public Formatter<typename ObjFlags::Type> {
  template <typename TStream>
  void Format(ObjFlags value, FormatContext<TStream>& ctx) const {
    Formatter<typename ObjFlags::Type>::Format(
        static_cast<typename ObjFlags::Type>(value), ctx);
  }
};

}  // namespace ae

#endif  // AETHER_OBJ_OBJ_ID_H_ */

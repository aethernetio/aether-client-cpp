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
#include <string>

#include "aether/mstream.h"

namespace ae {

class ObjId {
 public:
  using Type = std::uint32_t;

  static ObjId GenerateUnique();

  ObjId() { Invalidate(); }

  constexpr ObjId(Type i) : id_{i} {}

  constexpr Type id() const { return id_; }

  void Invalidate() { id_ = 0; }
  constexpr bool IsValid() const { return id_ != 0; }
  constexpr bool operator<(const ObjId& i) const { return id_ < i.id_; }
  constexpr bool operator!=(const ObjId& i) const { return id_ != i.id_; }
  constexpr bool operator==(const ObjId& i) const { return id_ == i.id_; }

  ObjId& operator+=(Type i) {
    id_ += i;
    return *this;
  }
  constexpr ObjId operator+(Type i) const { return ObjId{id_ + i}; }

  template <typename Ob>
  friend omstream<Ob>& operator<<(omstream<Ob>& s, const ObjId& i) {
    return s << i.id_;
  }
  template <typename Ib>
  friend imstream<Ib>& operator>>(imstream<Ib>& s, ObjId& i) {
    return s >> i.id_;
  }

  std::string ToString() const { return std::to_string(id_); }

 protected:
  Type id_;
};

class ObjFlags {
 public:
  enum {
    // The object is not loaded with deserialization. Load method must be
    // used for loading.
    kUnloadedByDefault = 1,
    kUnloaded = 2,
  };

  ObjFlags(uint8_t v) : value_(v) {}
  ObjFlags() = default;

  operator std::uint8_t&() { return value_; }

  template <typename Ob>
  friend omstream<Ob>& operator<<(omstream<Ob>& s, const ObjFlags& i) {
    return s << i.value_;
  }
  template <typename Ib>
  friend imstream<Ib>& operator>>(imstream<Ib>& s, ObjFlags& i) {
    return s >> i.value_;
  }

 private:
  std::uint8_t value_ = 0;
};

}  // namespace ae

#endif  // AETHER_OBJ_OBJ_ID_H_ */

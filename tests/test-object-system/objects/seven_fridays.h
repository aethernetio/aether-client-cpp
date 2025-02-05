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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_SEVEN_FRIDAYS_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_SEVEN_FRIDAYS_H_

#include <string>

#include "aether/obj/obj.h"

namespace ae {
// First version of Friday object
class Friday0 : public Obj {
  AE_OBJECT(Friday0, Obj, 0)

  Friday0() = default;

 public:
  explicit Friday0(Domain* domain) : Obj{domain} {}

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
  }
};

// add two new field
class Friday1 : public Obj {
  AE_OBJECT(Friday1, Obj, 1)

  Friday1() = default;

 public:
  explicit Friday1(Domain* domain) : Obj{domain} {}

  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    dnv(*base_ptr_);
    a = 22;
    b = 23;
  }

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(a, b);
  }

  template <typename Dnv>
  void Save(Version<0>, Dnv& dnv) const {
    dnv(*base_ptr_);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(a, b);
  }

  int a;
  int b;
};

// change one field type and remove another
class Friday2 : public Obj {
  AE_OBJECT(Friday2, Obj, 2)

  Friday2() = default;

 public:
  explicit Friday2(Domain* domain) : Obj{domain} {}

  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    dnv(*base_ptr_);
  }

  template <typename Dnv>
  void Load(Version<1>, Dnv& dnv) {
    int _b{};
    int _a{};
    // b may be ignored completely
    dnv(_a, _b);
    // old a value converted to new type in case there is no new version state
    // data
    a = static_cast<float>(_a);
  }

  template <typename Dnv>
  void Load(Version<2>, Dnv& dnv) {
    float _a{};
    dnv(_a);
    // no default value provided! use it!
    if (_a != 0.f) {
      a = _a;
    }
  }

  template <typename Dnv>
  void Save(Version<0>, Dnv& dnv) const {
    dnv(*base_ptr_);
  }

  template <typename Dnv>
  void Save(Version<1>, Dnv& dnv) const {
    int _b{};
    int _a{static_cast<int>(a)};
    dnv(_a, _b);
  }

  template <typename Dnv>
  void Save(Version<2>, Dnv& dnv) const {
    dnv(a);
  }

  // default value provided if there is no saved state to load data
  float a;
};

class Hoopa : public Obj {
  AE_OBJECT(Hoopa, Obj, 0)

 protected:
  Hoopa() = default;

 public:
  explicit Hoopa(Domain* domain) : Obj{domain} {}

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(x);
  }

  std::string x;
};

// change one field type and remove another
class Friday3 : public Hoopa {
  AE_OBJECT(Friday3, Hoopa, 3)

  Friday3() = default;

 public:
  explicit Friday3(Domain* domain) : Hoopa(domain) {}

  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    // load Hoopa class
    dnv(*base_ptr_);
  }

  template <typename Dnv>
  void Load(Version<1>, Dnv& dnv) {
    int _b;
    int _a;
    // b may be ignored completely
    dnv(_a, _b);
    // old a value converted to new type in case there is no new version state
    // data
    a = static_cast<float>(_a);
  }

  template <typename Dnv>
  void Load(Version<2>, Dnv& dnv) {
    float _a;
    dnv(_a);
    // no default value provided! use it!
    if (_a != 0.f) {
      a = _a;
    }
  }

  // provide for Load/Save function for new Version, is not required if there is
  // nothing to load or save
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {}

  template <typename Dnv>
  void Save(Version<0>, Dnv& dnv) const {
    dnv(*base_ptr_);
  }

  template <typename Dnv>
  void Save(Version<1>, Dnv& dnv) const {
    int _b{};
    int _a{static_cast<int>(a)};
    dnv(_a, _b);
  }

  template <typename Dnv>
  void Save(Version<2>, Dnv& dnv) const {
    dnv(a);
  }

  template <typename Dnv>
  void Save(Version<3>, Dnv& dnv) const {}

  // default value provided if there is no saved state to load data
  float a;
};

}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_SEVEN_FRIDAYS_H_

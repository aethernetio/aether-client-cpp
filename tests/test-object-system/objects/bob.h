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

#ifndef TESTS_TEST_OBJECT_SYSTEM_OBJECTS_BOB_H_
#define TESTS_TEST_OBJECT_SYSTEM_OBJECTS_BOB_H_

#include "aether/obj/obj.h"

#include "objects/foo.h"

namespace ae {
class Bob : public Obj {
  AE_OBJECT(Bob, Obj, 0)

 public:
  explicit Bob(Domain* domain) : Obj{domain} {
    foo_prefab = domain->CreateObj<Foo>();
    foo_prefab.SetFlags(ObjFlags::kUnloadedByDefault);
  }

  Foo::ptr CreateFoo() { return domain_->LoadCopy(foo_prefab); }

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(foo_prefab);
  }

  Foo::ptr foo_prefab;
};

class BobsMother : public Obj {
  AE_OBJECT(BobsMother, Obj, 0)

 public:
  explicit BobsMother(Domain* domain) : Obj{domain} {
    bob_prefab = domain->CreateObj<Bob>();
    bob_prefab.SetFlags(ObjFlags::kUnloadedByDefault);
  }

  Bob::ptr CreateBob() { return domain_->LoadCopy(bob_prefab); }

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(bob_prefab);
  }

  Bob::ptr bob_prefab;
};

class BobsFather : public Obj {
  AE_OBJECT(BobsFather, Obj, 0)
 public:
  explicit BobsFather(Domain* domain) : Obj{domain} {}

  Bob::ptr const& GetBob() const { return bob_; }
  void SetBob(Bob::ptr bob) { bob_ = bob; }

  template <typename Dnv>
  void Visit(Dnv& dnv) {
    dnv(*base_ptr_);
    dnv(bob_);
  }

 private:
  Bob::ptr bob_;
};
}  // namespace ae

#endif  // TESTS_TEST_OBJECT_SYSTEM_OBJECTS_BOB_H_

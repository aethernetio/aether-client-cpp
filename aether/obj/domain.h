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

#ifndef AETHER_OBJ_DOMAIN_H_
#define AETHER_OBJ_DOMAIN_H_

#include <map>
#include <set>
#include <vector>
#include <cstdint>
#include <cassert>
#include <utility>
#include <type_traits>

#include "aether/common.h"

#include "aether/mstream_buffers.h"
#include "aether/mstream.h"

#include "aether/obj/domain_tree.h"
#include "aether/obj/version_iterator.h"
#include "aether/obj/visitor_mstream.h"

#include "aether/obj/registry.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/obj_ptr.h"
#include "aether/ptr/ptr_view.h"

namespace ae {
class Domain;
class Obj;

class IDomainFacility {
 public:
  virtual ~IDomainFacility() = default;

  virtual void Store(const ObjId& obj_id, std::uint32_t class_id,
                     std::uint8_t version, const std::vector<uint8_t>& os) = 0;
  virtual std::vector<std::uint32_t> Enumerate(const ObjId& obj_id) = 0;
  virtual void Load(const ObjId& obj_id, std::uint32_t class_id,
                    std::uint8_t version, std::vector<uint8_t>& is) = 0;

  // TODO: where should we use it?
  virtual void Remove(const ObjId& obj_id) = 0;
};

struct DomainCycleDetector {
  struct Node {
    bool operator==(const Node& other) const {
      return id == other.id && class_id == other.class_id;
    }
    bool operator<(const Node& other) const {
      return id < other.id || (id == other.id && class_id < other.class_id);
    }

    ObjId id;
    std::uint32_t class_id;
  };

  template <typename T>
  bool IsVisited(T const* obj) const {
    auto node = Node{obj->GetId(), T::kClassId};
    return visited_nodes.find(node) != visited_nodes.end();
  }
  template <typename T>
  void Add(T const* obj) {
    visited_nodes.insert(Node{obj->GetId(), T::kClassId});
  }

  std::set<Node> visited_nodes;
};

template <typename Ob>
struct DomainBufferWriter : public Ob {
  Domain* domain{};
  using Ob::Ob;
};

template <typename Ib>
struct DomainBufferReader : public Ib {
  Domain* domain{};
  using Ib::Ib;
};

class Domain {
 public:
  Domain(TimePoint p, IDomainFacility& facility);

  TimePoint Update(TimePoint p);

  // Create new object and add it to the domain
  template <typename TClass, typename... TArgs>
  ObjPtr<TClass> CreateObj(ObjId obj_id, TArgs&&... args);
  template <typename TClass, typename TArg, typename... TArgs>
  ObjPtr<TClass> CreateObj(TArg&& arg1, TArgs&&... args);
  template <typename TClass>
  ObjPtr<TClass> CreateObj();

  // Load saved state of object
  template <typename T>
  void LoadRoot(ObjPtr<T>& ptr);
  // Save state of object
  template <typename T>
  void SaveRoot(ObjPtr<T> const& ptr);
  // Load a copy of object
  template <typename T>
  ObjPtr<T> LoadCopy(ObjPtr<T> const& ref,
                     ObjId copy_id = ObjId::GenerateUnique());

  template <typename T>
  void LoadRootImpl(ObjPtr<T>& ptr);
  template <typename T>
  void Load(T& obj);
  template <typename T, auto V>
  void LoadVersion(Version<V> version, T& obj);

  template <typename T>
  void SaveRootImpl(ObjPtr<T> const& ptr);
  template <typename T>
  void Save(T const& obj);
  template <typename T, auto V>
  void SaveVersion(Version<V> version, T const& obj);

  // Search for the object by obj_id.
  ObjPtr<Obj> Find(ObjId obj_id) const;

  void AddObject(ObjId id, ObjPtr<Obj> const& obj);
  void RemoveObject(Obj* obj);

 private:
  ObjPtr<Obj> ConstructObj(Factory const& factory, ObjId id);

  bool IsLast(uint32_t class_id) const;
  bool IsExisting(uint32_t class_id) const;

  Factory* GetMostRelatedFactory(ObjId id);
  Factory* FindClassFactory(Obj const& obj);

  TimePoint update_time_{};

  IDomainFacility& facility_;
  Registry registry_{};

  std::map<std::uint32_t, PtrView<Obj>> id_objects_{};

  DomainCycleDetector cycle_detector_{};
};

template <typename TClass, typename... TArgs>
ObjPtr<TClass> Domain::CreateObj(ObjId obj_id, TArgs&&... args) {
  static_assert(std::is_constructible_v<TClass, TArgs..., Domain*>,
                "Class must be constructible passed arguments and Domain*");

  // allocate object first and add it to the list
  auto object = ObjPtr{MakePtr<TClass>(std::forward<TArgs>(args)..., this)};
  AddObject(obj_id, object);
  object.SetId(obj_id);
  return object;
}

template <typename TClass, typename TArg, typename... TArgs>
ObjPtr<TClass> Domain::CreateObj(TArg&& arg1, TArgs&&... args) {
  if constexpr (std::is_constructible_v<ObjId, TArg>) {
    // if first arg is object id
    return CreateObj<TClass>(ObjId{static_cast<ObjId::Type>(arg1)},
                             std::forward<TArgs>(args)...);
  } else {
    return CreateObj<TClass>(ObjId::GenerateUnique(), std::forward<TArg>(arg1),
                             std::forward<TArgs>(args)...);
  }
}

template <typename TClass>
ObjPtr<TClass> Domain::CreateObj() {
  return CreateObj<TClass>(ObjId::GenerateUnique());
}

template <typename T>
void Domain::LoadRoot(ObjPtr<T>& ptr) {
  cycle_detector_ = {};
  LoadRootImpl(ptr);
}

template <typename T>
void Domain::SaveRoot(ObjPtr<T> const& ptr) {
  cycle_detector_ = {};
  SaveRootImpl(ptr);
}

template <typename T>
ObjPtr<T> Domain::LoadCopy(ObjPtr<T> const& ref, ObjId copy_id) {
  cycle_detector_ = {};

  auto obj_id = ref.GetId();
  auto obj_flags = ref.GetFlags();
  if (!obj_id.IsValid() || !copy_id.IsValid()) {
    return {};
  }
  // if already loaded
  if (auto o = Find(copy_id); o) {
    return o;
  }

  auto* factory = GetMostRelatedFactory(obj_id);
  if (!factory) {
    assert(false);
    return {};
  }

  ObjPtr<T> ptr = ConstructObj(*factory, copy_id);
  ptr.SetFlags(obj_flags & ~ObjFlags::kUnloaded &
               ~ObjFlags::kUnloadedByDefault);
  // temporary set obj_id
  ptr.SetId(obj_id);
  ptr = factory->load(this, ptr);
  // return new id
  ptr.SetId(copy_id);
  return ptr;
}

template <typename T>
void Domain::LoadRootImpl(ObjPtr<T>& ptr) {
  auto obj_id = ptr.GetId();
  auto obj_flags = ptr.GetFlags();

  if (!obj_id.IsValid()) {
    return;
  }
  // if already loaded
  if (auto o = Find(obj_id); o) {
    ptr = std::move(o);
    return;
  }

  if (auto* factory = GetMostRelatedFactory(obj_id); factory) {
    ptr = ConstructObj(*factory, ptr.GetId());
    ptr.SetFlags(obj_flags & ~ObjFlags::kUnloaded);
    ptr = factory->load(this, ptr);
  }
}

template <typename T>
void Domain::Load(T& obj) {
  if (cycle_detector_.IsVisited(&obj)) {
    return;
  }
  cycle_detector_.Add(&obj);

  if constexpr (HasAnyVersionedLoad<T>::value) {
    constexpr auto version_bounds = VersionedLoadMinMax<T>::value;
    IterateVersions<HasVersionedLoad, version_bounds.first,
                    version_bounds.second>(
        obj, [this](auto version, auto& obj) { LoadVersion(version, obj); });
  } else {
    LoadVersion(T::kCurrentVersion, obj);
  }
}

template <typename T, auto V>
void Domain::LoadVersion(Version<V> version, T& obj) {
  std::vector<uint8_t> input_data;
  facility_.Load(obj.GetId(), T::kClassId, V, input_data);

  DomainBufferReader<VectorReader<>> reader(input_data);
  reader.domain = this;
  imstream<DomainBufferReader<VectorReader<>>> is(reader);

  auto visitor_func = [&is](auto& v) {
    is >> v;
    // do not propagate for objects
    return false;
  };

  // if T has any versioned, it also must have Load for this version
  if constexpr (HasAnyVersionedLoad<T>::value) {
    CycleDetector cd;
    auto dnv = DomainNodeVisitor<ExplicitVisitPolicy, decltype(visitor_func)>{
        std::move(visitor_func), cd};
    obj.Load(version, dnv);
  } else {
    // load or deserialize object
    DomainTree<ExplicitVisitPolicy>::Visit(obj, std::move(visitor_func));
  }
}

template <typename T>
void Domain::SaveRootImpl(ObjPtr<T> const& ptr) {
  if (!ptr) {
    return;
  }
  if (auto* factory = FindClassFactory(*ptr); factory) {
    factory->save(this, ptr);
  }
}

template <typename T>
void Domain::Save(T const& obj) {
  if (cycle_detector_.IsVisited(&obj)) {
    return;
  }
  cycle_detector_.Add(&obj);

  if constexpr (HasAnyVersionedSave<T>::value) {
    constexpr auto version_bounds = VersionedSaveMinMax<T>::value;
    IterateVersions<HasVersionedSave, version_bounds.second,
                    version_bounds.first>(
        obj, [this](auto version, auto& obj) { SaveVersion(version, obj); });
  } else {
    SaveVersion(T::kCurrentVersion, obj);
  }
}

template <typename T, auto V>
void Domain::SaveVersion(Version<V> version, T const& obj) {
  std::vector<uint8_t> output_data;
  DomainBufferWriter<VectorWriter<>> writer(output_data);
  writer.domain = this;
  omstream<DomainBufferWriter<VectorWriter<>>> os(writer);

  auto visitor_func = [&os](auto& v) {
    os << v;
    // do not propagate for objects
    return false;
  };

  if constexpr (HasAnyVersionedSave<T>::value) {
    CycleDetector cd;
    auto dnv = DomainNodeVisitor<ExplicitVisitPolicy, decltype(visitor_func)>{
        std::move(visitor_func), cd};
    obj.Save(version, dnv);
  } else {
    // load or deserialize object
    DomainTree<ExplicitVisitPolicy>::Visit(const_cast<T&>(obj),
                                           std::move(visitor_func));
  }

  facility_.Store(obj.GetId(), T::kClassId, Version<V>::value, output_data);
}

template <typename T, typename Ib>
imstream<DomainBufferReader<Ib>>& operator>>(
    imstream<DomainBufferReader<Ib>>& is, ObjPtr<T>& ptr) {
  ObjId id;
  ObjFlags flags;
  is >> id >> flags;
  ptr.SetId(id);
  ptr.SetFlags(flags);
  if ((flags & ObjFlags::kUnloadedByDefault) || (flags & ObjFlags::kUnloaded)) {
    return is;
  }

  is.ib_.domain->LoadRootImpl(ptr);
  return is;
}

template <typename T, typename Ob>
omstream<DomainBufferWriter<Ob>>& operator<<(
    omstream<DomainBufferWriter<Ob>>& os, ObjPtr<T> const& ptr) {
  auto id = ptr.GetId();
  auto flags = ptr.GetFlags();
  os << id << flags;
  os.ob_.domain->SaveRootImpl(ptr);
  return os;
}

template <typename T, typename Ib>
std::enable_if_t<std::is_base_of_v<Obj, T>, imstream<DomainBufferReader<Ib>>&>
operator>>(imstream<DomainBufferReader<Ib>>& is, T& obj) {
  is.ib_.domain->Load(obj);
  return is;
}

template <typename T, typename Ob>
std::enable_if_t<std::is_base_of_v<Obj, T>, omstream<DomainBufferWriter<Ob>>&>
operator<<(omstream<DomainBufferWriter<Ob>>& os, T const& obj) {
  os.ob_.domain->Save(obj);
  return os;
}

}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_H_

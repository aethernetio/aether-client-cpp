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
#include <cstdint>
#include <cassert>
#include <utility>
#include <type_traits>

#include "aether/common.h"

#include "aether/mstream.h"

#include "aether/ptr/ptr_view.h"
#include "aether/reflect/reflect.h"
#include "aether/reflect/domain_visitor.h"

#include "aether/obj/obj_id.h"
#include "aether/obj/registry.h"
#include "aether/obj/idomain_storage.h"
#include "aether/obj/version_iterator.h"

namespace ae {
class Obj;
class Domain;
class DomainGraph;

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

  bool Add(std::uint32_t class_id, ObjId obj_id) {
    auto [_, ok] = visited_nodes.insert(Node{obj_id, class_id});
    return ok;
  }

  std::set<Node> visited_nodes;
};

class DomainStorageReaderEmpty final : public IDomainStorageReader {
 public:
  void read(void*, std::size_t) override {}
  ReadResult result() const override { return ReadResult::kNo; }
  void result(ReadResult) override {}
};

struct DomainBufferWriter {
  using size_type = IDomainStorageReader::size_type;

  void write(void const* data, std::size_t size) { writer->write(data, size); }

  ObjId id;
  DomainGraph* domain_graph{};
  IDomainStorageWriter* writer;
};

struct DomainBufferReader {
  using size_type = IDomainStorageReader::size_type;

  void read(void* data, std::size_t size) { reader->read(data, size); }
  ReadResult result() const { return reader->result(); }
  void result(ReadResult result) { reader->result(result); }

  ObjId id;
  DomainGraph* domain_graph{};
  IDomainStorageReader* reader;
};

/**
 * \brief Operations on graph of objects in domain.
 * For load/save a new graph create a new DomainGraph.
 */
class DomainGraph {
 public:
  explicit DomainGraph(Domain* domain);

  // Load saved state of object
  template <typename T>
  Ptr<T> LoadPtr(ObjId obj_id);
  // Save state of object
  template <typename T>
  void SavePtr(Ptr<T> const& ptr, ObjId obj_id);
  // Load a copy of object
  template <typename T>
  Ptr<T> LoadCopy(ObjId ref_id, ObjId copy_id);

  Ptr<Obj> LoadRootImpl(ObjId obj_id);
  Ptr<Obj> LoadCopyImpl(ObjId ref_id, ObjId copy_id);
  void SaveRootImpl(Ptr<Obj> const& ptr, ObjId obj_id);

  template <typename T>
  void Load(T& obj, ObjId obj_id);
  template <typename T, auto V>
  void LoadVersion(Version<V> version, T& obj, ObjId obj_id);

  template <typename T>
  void Save(T const& obj, ObjId obj_id);
  template <typename T, auto V>
  void SaveVersion(Version<V> version, T const& obj, ObjId obj_id);

  Domain* domain{};
  DomainCycleDetector cycle_detector{};

 private:
  std::unique_ptr<IDomainStorageReader> GetReader(DomainQuery const& query);
  std::unique_ptr<IDomainStorageWriter> GetWriter(DomainQuery const& query);
};

class Domain {
  friend class DomainGraph;

 public:
  Domain(TimePoint p, IDomainStorage& storage);

  TimePoint Update(TimePoint current_time);

  // Search for the object by obj_id.
  Ptr<Obj> Find(ObjId obj_id) const;

  void AddObject(ObjId id, Ptr<Obj> const& obj);
  void RemoveObject(Obj* obj);

 private:
  Ptr<Obj> ConstructObj(Factory const& factory, ObjId id);

  bool IsLast(uint32_t class_id) const;
  bool IsExisting(uint32_t class_id) const;

  Factory* FindClassFactory(std::uint32_t class_id);
  Factory* GetMostRelatedFactory(ObjId id);

  TimePoint update_time_;
  IDomainStorage* storage_;
  Registry* registry_;

  std::map<ObjId::Type, PtrView<Obj>> id_objects_;
};

template <typename T>
Ptr<T> DomainGraph::LoadPtr(ObjId obj_id) {
  Ptr<T> ptr = LoadRootImpl(obj_id);
  return ptr;
}

template <typename T>
void DomainGraph::SavePtr(Ptr<T> const& ptr, ObjId obj_id) {
  SaveRootImpl(ptr, obj_id);
}

template <typename T>
Ptr<T> DomainGraph::LoadCopy(ObjId ref_id, ObjId copy_id) {
  return Ptr<T>{LoadCopyImpl(ref_id, copy_id)};
}

template <typename T>
void DomainGraph::Load(T& obj, ObjId obj_id) {
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return;
  }

  if constexpr (HasAnyVersionedLoad<T>::value) {
    constexpr auto version_bounds = VersionedLoadMinMax<T>::value;
    IterateVersions<HasVersionedLoad, version_bounds.first,
                    version_bounds.second>(
        obj, [this, obj_id](auto version, auto& obj) {
          this->LoadVersion(version, obj, obj_id);
        });
  } else {
    LoadVersion(T::kCurrentVersion, obj, obj_id);
  }
}

template <typename T, auto V>
void DomainGraph::LoadVersion(Version<V> version, T& obj, ObjId obj_id) {
  auto storage_reader = GetReader({obj_id, T::kClassId, V});
  DomainBufferReader reader{obj_id, this, storage_reader.get()};
  imstream is(reader);

  auto visitor_func = [&is](auto& value) {
    is >> value;
    return false;
  };

  // if T has any versioned, it also must have Load for this version
  if constexpr (HasAnyVersionedLoad<T>::value) {
    VersionNodeVisitor visitor{std::move(visitor_func)};
    obj.Load(version, visitor);
  } else {
    // load or deserialize object
    reflect::DomainVisit(obj, std::move(visitor_func));
  }
}

template <typename T>
void DomainGraph::Save(T const& obj, ObjId obj_id) {
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return;
  }

  if constexpr (HasAnyVersionedSave<T>::value) {
    constexpr auto version_bounds = VersionedSaveMinMax<T>::value;
    IterateVersions<HasVersionedSave, version_bounds.second,
                    version_bounds.first>(
        obj, [this, obj_id](auto version, auto& obj) {
          this->SaveVersion(version, obj, obj_id);
        });
  } else {
    SaveVersion(T::kCurrentVersion, obj, obj_id);
  }
}

template <typename T, auto V>
void DomainGraph::SaveVersion(Version<V> version, T const& obj, ObjId obj_id) {
  auto storage_writer = GetWriter({obj_id, T::kClassId, V});
  DomainBufferWriter writer{obj_id, this, storage_writer.get()};
  omstream os(writer);

  auto visitor_func = [&os](auto const& value) {
    os << value;
    return false;
  };

  if constexpr (HasAnyVersionedSave<T>::value) {
    VersionNodeVisitor visitor{std::move(visitor_func)};
    obj.Save(version, visitor);
  } else {
    // load or deserialize object
    reflect::DomainVisit(obj, std::move(visitor_func));
  }
}

template <typename T>
std::enable_if_t<std::is_base_of_v<Obj, T>, imstream<DomainBufferReader>&>
operator>>(imstream<DomainBufferReader>& is, T& obj) {
  is.ib_.domain_graph->Load(obj, is.ib_.id);
  return is;
}

template <typename T>
std::enable_if_t<std::is_base_of_v<Obj, T>, omstream<DomainBufferWriter>&>
operator<<(omstream<DomainBufferWriter>& os, T const& obj) {
  os.ob_.domain_graph->Save(obj, os.ob_.id);
  return os;
}

}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_H_

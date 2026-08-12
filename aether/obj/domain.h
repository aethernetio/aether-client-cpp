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

#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "aether/clock.h"

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"
#include "aether/ptr/ptr_view.h"
#include "aether/tiered_int_serializer.h"

#include "aether/obj/idomain_storage.h"
#include "aether/obj/obj_id.h"
#include "aether/obj/registry.h"
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

struct DomainBuffer {
  seri::SeriResult Write(seri::SizeWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_eof};
    }
    return writer->Write(data);
  }

  seri::SeriResult Write(seri::DataWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_eof};
    }
    return writer->Write(data);
  }

  seri::SeriResult Read(seri::SizeReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_eof};
    }
    return reader->Read(data);
  }
  seri::SeriResult Read(seri::DataReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_eof};
    }
    return reader->Read(data);
  }

  ObjId id;
  DomainGraph* domain_graph{};
  IDomainStorageWriter* writer{nullptr};
  IDomainStorageReader* reader{nullptr};
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
  seri::SeriResult Load(T& obj, ObjId obj_id);
  template <typename T, auto V>
  seri::SeriResult LoadVersion(Version<V> version, T& obj, ObjId obj_id);

  template <typename T>
  seri::SeriResult Save(T const& obj, ObjId obj_id);
  template <typename T, auto V>
  seri::SeriResult SaveVersion(Version<V> version, T const& obj, ObjId obj_id);

  Domain* domain{};
  DomainCycleDetector cycle_detector{};

 private:
  DomainLoad GetReader(DomainQuery const& query);
  std::unique_ptr<IDomainStorageWriter> GetWriter(DomainQuery const& query);
};

class Domain {
  friend class DomainGraph;

 public:
  Domain(TimePoint p, IDomainStorage& storage);

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
seri::SeriResult DomainGraph::Load(T& obj, ObjId obj_id) {
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return Ok{seri::good};
  }

  if constexpr (HasAnyVersionedLoad<T>::value) {
    auto result = seri::SeriResult{Ok{seri::good}};
    version_iterator<VersionLoadTrait>(
        obj, [this, obj_id, &result](auto version, auto& obj) {
          if (result.IsErr()) {
            return;
          }
          result = this->LoadVersion(version, obj, obj_id);
        });
    return result;
  } else {
    return LoadVersion(T::kCurrentVersion, obj, obj_id);
  }
}

struct LoadVisitor {
  void operator()(auto& v) {
    if (res) {
      res = bin_archive.Load(v);
    }
  }

  seri::BinaryArchive<DomainBuffer>& bin_archive;
  seri::SeriResult res{Ok{seri::good}};
};

template <typename T, auto V>
seri::SeriResult DomainGraph::LoadVersion(Version<V> version, T& obj,
                                          ObjId obj_id) {
  auto load = GetReader({obj_id, T::kClassId, V});
  if (load.result != DomainLoadResult::kLoaded) {
    return Ok{seri::good};
  }

  auto bin_archive =
      seri::BinaryArchive{DomainBuffer{.id = obj_id,
                                       .domain_graph = this,
                                       .writer = {},
                                       .reader = load.reader.get()}};

  auto load_visitor = LoadVisitor{bin_archive};

  // if T has any versioned, it also must have Load for this version
  if constexpr (HasAnyVersionedLoad<T>::value) {
    auto visitor = VersionNodeVisitor{load_visitor};
    obj.Load(version, visitor);
  } else {
    // load or deserialize object
    domain_visitor::DomainVisit(obj, load_visitor);
  }
  return load_visitor.res;
}

template <typename T>
seri::SeriResult DomainGraph::Save(T const& obj, ObjId obj_id) {
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return Ok{seri::good};
  }

  if constexpr (HasAnyVersionedSave<T>::value) {
    auto result = seri::SeriResult{Ok{seri::good}};
    version_iterator<VersionSaveTrait>(
        obj, [this, obj_id, &result](auto version, auto& obj) {
          if (result.IsErr()) {
            return;
          }
          result = this->SaveVersion(version, obj, obj_id);
        });
    return result;
  } else {
    return SaveVersion(T::kCurrentVersion, obj, obj_id);
  }
}

struct SaveVisitor {
  void operator()(auto const& v) {
    if (res) {
      res = bin_archive.Save(v);
    }
  }

  seri::BinaryArchive<DomainBuffer>& bin_archive;
  seri::SeriResult res{Ok{seri::good}};
};

template <typename T, auto V>
seri::SeriResult DomainGraph::SaveVersion(Version<V> version, T const& obj,
                                          ObjId obj_id) {
  auto storage_writer = GetWriter({obj_id, T::kClassId, V});

  auto bin_archive =
      seri::BinaryArchive{DomainBuffer{.id = obj_id,
                                       .domain_graph = this,
                                       .writer = storage_writer.get(),
                                       .reader = {}}};

  auto save_visitor = SaveVisitor{bin_archive};

  if constexpr (HasAnyVersionedSave<T>::value) {
    auto visitor = VersionNodeVisitor{save_visitor};
    obj.Save(version, visitor);
  } else {
    // load or deserialize object
    domain_visitor::DomainVisit(
        obj, save_visitor,
        domain_visitor::PolicyConst<domain_visitor::VisitPolicy::kShallow>{});
  }
  return save_visitor.res;
}

namespace seri {
template <typename T>
  requires(std::is_base_of_v<Obj, T>)
struct Serializer<BinaryArchive<DomainBuffer>, T> {
  using Archive = BinaryArchive<DomainBuffer>;

  SeriResult Seri(Archive& arch, Meta<T const> meta_val) const {
    return arch.buffer().domain_graph->Save(meta_val.value, arch.buffer().id);
  }

  SeriResult Deseri(Archive& arch, Meta<T> meta_val) const {
    return arch.buffer().domain_graph->Load(meta_val.value, arch.buffer().id);
  }
};

template <>
struct Serializer<BinaryArchive<DomainBuffer>, std::string> {
  SeriResult Deseri(BinaryArchive<DomainBuffer>& archive,
                    Meta<std::string> val) const;
  SeriResult Seri(BinaryArchive<DomainBuffer>& archive,
                  Meta<std::string const> val) const;
};

template <>
struct Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>> {
  SeriResult Deseri(BinaryArchive<DomainBuffer>& archive,
                    Meta<std::vector<std::uint8_t>> val) const;
  SeriResult Seri(BinaryArchive<DomainBuffer>& archive,
                  Meta<std::vector<std::uint8_t> const> val) const;
};

}  // namespace seri
}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_H_

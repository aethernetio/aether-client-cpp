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
#include <utility>

#include "aether/mstream.h"
#include "aether/obj/idomain_storage.h"
#include "aether/obj/version_iterator.h"

namespace ae {
class Obj;
class Domain;

struct DomainBufferWriter {
  using size_type = IDomainStorageReader::size_type;

  DomainBufferWriter(Domain* d, IDomainStorageWriter& w)
      : domain{d}, writer{&w} {}

  void write(void const* data, std::size_t size) { writer->write(data, size); }

  Domain* domain{};
  IDomainStorageWriter* writer;
};

struct DomainBufferReader {
  using size_type = IDomainStorageReader::size_type;

  DomainBufferReader(Domain* d, IDomainStorageReader& r)
      : domain{d}, reader{&r} {}

  void read(void* data, std::size_t size) { reader->read(data, size); }

  Domain* domain{};
  IDomainStorageReader* reader;
};

class Domain {
 public:
  explicit Domain(IDomainStorage& storage);

  template <typename T>
  void Load(T& obj, DataKey key);
  template <typename T, auto V>
  void LoadVersion(Version<V> version, T& obj, DataKey key);

  template <typename T>
  void Save(T const& obj, DataKey key);
  template <typename T, auto V>
  void SaveVersion(Version<V> version, T const& obj, DataKey key);

 private:
  std::unique_ptr<IDomainStorageReader> GetReader(DataKey key,
                                                  std::uint8_t version);
  std::unique_ptr<IDomainStorageWriter> GetWriter(DataKey key,
                                                  std::uint8_t version);

  IDomainStorage* storage_;
};

template <typename T>
void Domain::Load(T& obj, DataKey key) {
  if constexpr (HasAnyVersionedLoad<T>::value) {
    constexpr auto version_bounds = VersionedLoadMinMax<T>::value;
    IterateVersions<HasVersionedLoad, version_bounds.first,
                    version_bounds.second>(obj, [&](auto version, auto& obj) {
      this->LoadVersion(version, obj, key);
    });
  } else {
    LoadVersion(T::kCurrentVersion, obj, key);
  }
}

template <typename T, auto V>
void Domain::LoadVersion(Version<V> version, T& obj, DataKey key) {
  auto storage_reader = GetReader(key, V);
  if (!storage_reader) {
    return;
  }
  DomainBufferReader reader{this, *storage_reader};
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
void Domain::Save(T const& obj, DataKey key) {
  if constexpr (HasAnyVersionedSave<T>::value) {
    constexpr auto version_bounds = VersionedSaveMinMax<T>::value;
    IterateVersions<HasVersionedSave, version_bounds.second,
                    version_bounds.first>(obj, [&](auto version, auto& obj) {
      this->SaveVersion(version, obj, key);
    });
  } else {
    SaveVersion(T::kCurrentVersion, obj, key);
  }
}

template <typename T, auto V>
void Domain::SaveVersion(Version<V> version, T const& obj, DataKey key) {
  auto storage_writer = GetWriter(key, V);
  DomainBufferWriter writer{this, *storage_writer};
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
}  // namespace ae

#endif  // AETHER_OBJ_DOMAIN_H_

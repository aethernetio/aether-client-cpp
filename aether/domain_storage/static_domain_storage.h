/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_
#define AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_

#include "aether/obj/idomain_storage.h"
#include "aether/domain_storage/static_object_types.h"

#include "aether/tele/tele.h"

namespace ae {
class StaticDomainStorageReader final : public IDomainStorageReader {
 public:
  explicit StaticDomainStorageReader(Span<std::uint8_t const> const& d);

  void read(void* out, std::size_t size) override;

  ReadResult result() const override;
  void result(ReadResult) override;

 private:
  Span<std::uint8_t const> const* data;
  std::size_t offset;
};

template <std::size_t ObjectCount, std::size_t ClassDataCount>
class StaticDomainStorage final : public IDomainStorage {
 public:
  constexpr explicit StaticDomainStorage(
      StaticDomainData<ObjectCount, ClassDataCount> const& sdd)
      : static_domain_data_{&sdd} {}

  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& /*query*/) override {
    // does not supported
    return {};
  }

  ClassList Enumerate(ObjId const& obj_id) override {
    // object_map is defined in FS_INIT
    auto const classes = static_domain_data_->object_map.find(obj_id.id());
    if (classes == std::end(static_domain_data_->object_map)) {
      AE_TELED_ERROR("Obj not found {}", obj_id.ToString());
      return {};
    }
    AE_TELED_DEBUG("Enumerated for obj {} classes {}", obj_id.ToString(),
                   classes->second);
    return ClassList{std::begin(classes->second), std::end(classes->second)};
  }

  DomainLoad Load(DomainQuery const& query) override {
    // state_map is defined in FS_INIT
    auto obj_path = ObjectPathKey{query.id.id(), query.class_id, query.version};
    auto const data = static_domain_data_->state_map.find(obj_path);
    if (data == std::end(static_domain_data_->state_map)) {
      AE_TELED_ERROR("Unable to find object id={}, class id={}, version={}",
                     query.id.ToString(), query.class_id,
                     static_cast<int>(query.version));
      return {DomainLoadResult::kEmpty, {}};
    }
    AE_TELED_DEBUG("Loaded object id={}, class id={}, version={}, size={}",
                   query.id.ToString(), query.class_id,
                   static_cast<int>(query.version), data->second.size());

    return DomainLoad{
        DomainLoadResult::kLoaded,
        std::make_unique<StaticDomainStorageReader>(data->second)};
  }

  void Remove(ObjId const& /*obj_id*/) override {
    // does not supported
  }
  void CleanUp() override {
    // does not supported
  }

 private:
  StaticDomainData<ObjectCount, ClassDataCount> const* static_domain_data_;
};

template <std::size_t ObjectCount, std::size_t ClassDataCount>
StaticDomainStorage(StaticDomainData<ObjectCount, ClassDataCount> const&)
    -> StaticDomainStorage<ObjectCount, ClassDataCount>;

}  // namespace ae

#endif  // AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_

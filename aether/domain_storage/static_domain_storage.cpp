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

#include "aether/domain_storage/static_domain_storage.h"

#if defined STATIC_DOMAIN_STORAGE_ENABLED

#  include <cassert>
#  include <iterator>
#  include <algorithm>

#  include "aether/domain_storage/static_object_types.h"

#  include "aether/domain_storage/domain_storage_tele.h"

#  include FS_INIT

namespace ae {
class StaticDomainStorageReader final : public IDomainStorageReader {
 public:
  explicit StaticDomainStorageReader(Span<std::uint8_t const> const& d)
      : data{&d}, offset{} {}

  void read(void* out, std::size_t size) override {
    assert((offset + size) <= data->size());
    std::copy(std::begin(*data) + offset, std::begin(*data) + offset + size,
              reinterpret_cast<std::uint8_t*>(out));
    offset += size;
  }

  ReadResult result() const override { return ReadResult::kYes; }
  void result(ReadResult) override {}

  Span<std::uint8_t const> const* data;
  std::size_t offset;
};

StaticDomainStorage::StaticDomainStorage() = default;
StaticDomainStorage::~StaticDomainStorage() = default;

std::unique_ptr<IDomainStorageWriter> StaticDomainStorage::Store(
    DomainQuiery const& /*query*/) {
  // not supported
  return {};
}

ClassList StaticDomainStorage::Enumerate(ObjId const& obj_id) {
  // object_map is defined in FS_INIT
  auto const classes = object_map.find(obj_id.id());
  if (classes == std::end(object_map)) {
    AE_TELE_ERROR(DsEnumObjIdNotFound, "Obj not found {}", obj_id.ToString());
    return {};
  }
  AE_TELE_DEBUG(DsEnumerated, "Enumerated for obj {} classes {}",
                obj_id.ToString(), classes->second);
  return ClassList{std::begin(classes->second), std::end(classes->second)};
}

DomainLoad StaticDomainStorage::Load(DomainQuiery const& query) {
  // state_map is defined in FS_INIT
  auto obj_path = ObjectPathKey{query.id.id(), query.class_id, query.version};
  auto const data = state_map.find(obj_path);
  if (data == std::end(state_map)) {
    AE_TELE_ERROR(DsLoadObjIdNoFound,
                  "Unable to find object id={}, class id={}, version={}",
                  query.id.ToString(), query.class_id,
                  static_cast<int>(query.version));
    return {DomainLoadResult::kEmpty, {}};
  }
  AE_TELE_DEBUG(DsObjLoaded,
                "Loaded object id={}, class id={}, version={}, size={}",
                query.id.ToString(), query.class_id,
                static_cast<int>(query.version), data->second.size());

  return DomainLoad{DomainLoadResult::kLoaded,
                    std::make_unique<StaticDomainStorageReader>(data->second)};
}

void StaticDomainStorage::Remove(ObjId const& /* obj_id */) {}

void StaticDomainStorage::CleanUp() {}

}  // namespace ae
#endif

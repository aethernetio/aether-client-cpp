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

 private:
  Span<std::uint8_t const> const* data;
  std::size_t offset;
};

template <std::size_t ObjectCount>
class StaticDomainStorage final : public IDomainStorage {
 public:
  constexpr explicit StaticDomainStorage(
      StaticDomainData<ObjectCount> const& sdd)
      : static_domain_data_{&sdd} {}

  std::unique_ptr<IDomainStorageWriter> Store(
      DataKey /* key */, std::uint8_t /* version */) override {
    // does not supported
    return {};
  }

  DomainLoad Load(DataKey key, std::uint8_t version) override {
    // state_map is defined in FS_INIT
    auto obj_path = ObjectPathKey{key, version};
    auto const data = static_domain_data_->state_map.find(obj_path);
    if (data == std::end(static_domain_data_->state_map)) {
      AE_TELED_ERROR("Unable to find object key={}, version={}", key,
                     static_cast<int>(version));
      return {DomainLoadResult::kEmpty, {}};
    }
    AE_TELED_DEBUG("Loaded object key={}, version={}, size={}", key,
                   static_cast<int>(version), data->second.size());

    return DomainLoad{
        DomainLoadResult::kLoaded,
        std::make_unique<StaticDomainStorageReader>(data->second)};
  }

  void Remove(DataKey /* key */) override {
    // does not supported
  }
  void CleanUp() override {
    // does not supported
  }

 private:
  StaticDomainData<ObjectCount> const* static_domain_data_;
};

template <std::size_t ObjectCount>
StaticDomainStorage(StaticDomainData<ObjectCount> const&)
    -> StaticDomainStorage<ObjectCount>;

}  // namespace ae

#endif  // AETHER_DOMAIN_STORAGE_STATIC_DOMAIN_STORAGE_H_
